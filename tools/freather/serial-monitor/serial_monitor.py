#!/usr/bin/env python3
"""Standalone Edgi-Talk serial monitor ported from the ABW test framework.

The legacy archive mixed UART I/O with pytest, Wi-Fi test cases, device
factories, routers, and command-expectation business logic. This module keeps
only the reusable serial-management behaviour: enumerate, open, receive,
display, log, transmit, and close.
"""

from __future__ import annotations

import argparse
import codecs
from datetime import datetime
from pathlib import Path
import queue
import re
import sys
import tempfile
import threading
import time
from typing import TextIO

import serial
from serial import SerialException
from serial.tools import list_ports


DEFAULT_BAUDRATE = 115_200
DEFAULT_READ_TIMEOUT = 0.10
DEFAULT_WRITE_TIMEOUT = 2.0
READ_CHUNK_SIZE = 4096
EXIT_COMMAND = "~."
LOG_FORMATS = ("text", "event")


class SerialLog:
    """Append serial data as readable RX text or detailed RX/TX events."""

    def __init__(
        self,
        path: Path | None,
        encoding: str,
        errors: str,
        log_format: str = "text",
    ) -> None:
        if log_format not in LOG_FORMATS:
            raise ValueError(f"unsupported log format: {log_format!r}")
        self.path = path
        self.encoding = encoding
        self.errors = errors
        self.log_format = log_format
        self._lock = threading.Lock()
        self._file: TextIO | None = None
        self._decoder = codecs.getincrementaldecoder(encoding)(errors=errors)

        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)
            # newline="" is required on Windows. It prevents TextIO from
            # translating a device CRLF into CRCRLF while keeping lone CR/LF.
            self._file = path.open(
                "a", encoding="utf-8", errors=errors, newline="", buffering=1
            )
            if log_format == "event":
                self._file.write(
                    f"{datetime.now().astimezone().isoformat()} EVENT monitor-open\n"
                )

    def record(self, direction: str, payload: bytes) -> None:
        if self._file is None:
            return
        with self._lock:
            if self.log_format == "text":
                # The readable log represents what the target printed. TX and
                # monitor events are deliberately omitted. An incremental
                # decoder keeps multibyte UTF-8 characters intact even when a
                # USB serial driver splits them across receive chunks.
                if direction != "RX":
                    return
                text = self._decoder.decode(payload)
                if text:
                    self._file.write(text)
                    self._file.flush()
                return

            text = payload.decode(self.encoding, errors=self.errors)
            timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
            self._file.write(
                f"{timestamp} {direction} bytes={payload.hex()} text={text!r}\n"
            )

    def event(self, message: str) -> None:
        if self._file is None or self.log_format != "event":
            return
        timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
        with self._lock:
            self._file.write(f"{timestamp} EVENT {message}\n")

    def close(self) -> None:
        if self._file is None:
            return
        with self._lock:
            if self.log_format == "text":
                tail = self._decoder.decode(b"", final=True)
                if tail:
                    self._file.write(tail)
            else:
                timestamp = datetime.now().astimezone().isoformat(
                    timespec="milliseconds"
                )
                self._file.write(f"{timestamp} EVENT monitor-close\n")
            self._file.flush()
            self._file.close()
        self._file = None


class SerialMonitor:
    """Own one serial port and one background receive thread."""

    def __init__(
        self,
        port: str,
        baudrate: int,
        *,
        encoding: str = "utf-8",
        decode_errors: str = "replace",
        read_timeout: float = DEFAULT_READ_TIMEOUT,
        write_timeout: float = DEFAULT_WRITE_TIMEOUT,
        rtscts: bool = False,
        xonxoff: bool = False,
        timestamps: bool = False,
        log_path: Path | None = None,
        log_format: str = "text",
        output: TextIO = sys.stdout,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.encoding = encoding
        self.decode_errors = decode_errors
        self.read_timeout = read_timeout
        self.write_timeout = write_timeout
        self.rtscts = rtscts
        self.xonxoff = xonxoff
        self.timestamps = timestamps
        self.output = output

        self._serial: serial.SerialBase | None = None
        self._stop = threading.Event()
        self._write_lock = threading.Lock()
        self._output_lock = threading.Lock()
        self._reader: threading.Thread | None = None
        self._reader_error: BaseException | None = None
        self._decoder = codecs.getincrementaldecoder(encoding)(errors=decode_errors)
        self.log = SerialLog(log_path, encoding, decode_errors, log_format)

    @property
    def is_open(self) -> bool:
        return self._serial is not None and self._serial.is_open

    @property
    def reader_error(self) -> BaseException | None:
        return self._reader_error

    def open(self) -> None:
        if self.is_open:
            return

        self._serial = serial.serial_for_url(
            self.port,
            baudrate=self.baudrate,
            timeout=self.read_timeout,
            write_timeout=self.write_timeout,
            rtscts=self.rtscts,
            xonxoff=self.xonxoff,
        )
        self._stop.clear()
        self._reader_error = None
        self.log.event(f"serial-open port={self.port!r} baud={self.baudrate}")
        self._reader = threading.Thread(
            target=self._receive_loop,
            name=f"serial-rx-{self.port}",
            daemon=True,
        )
        self._reader.start()

    def _receive_loop(self) -> None:
        assert self._serial is not None
        try:
            while not self._stop.is_set():
                waiting = self._serial.in_waiting
                size = min(max(waiting, 1), READ_CHUNK_SIZE)
                payload = self._serial.read(size)
                if not payload:
                    continue
                self.log.record("RX", payload)
                text = self._decoder.decode(payload)
                if text:
                    self._display(text)
        except (SerialException, OSError) as exc:
            if not self._stop.is_set():
                self._reader_error = exc
                self.log.event(f"receive-error {exc!r}")
                self._stop.set()

    def _display(self, text: str) -> None:
        # TextIO performs platform newline translation on Windows. Collapse a
        # complete CRLF here so a device CRLF is not rendered as CRCRLF. Raw
        # bytes remain unchanged in the evidence log.
        text = text.replace("\r\n", "\n")
        prefix = ""
        if self.timestamps:
            now = datetime.now().astimezone().isoformat(timespec="milliseconds")
            prefix = f"[{now}] "
        with self._output_lock:
            self.output.write(prefix + text)
            self.output.flush()

    def write_bytes(self, payload: bytes) -> int:
        if not self.is_open or self._serial is None:
            raise RuntimeError("serial port is not open")
        with self._write_lock:
            written = self._serial.write(payload)
            self._serial.flush()
        self.log.record("TX", payload[:written])
        return written

    def write_text(self, text: str, line_ending: str = "crlf") -> int:
        endings = {"none": "", "cr": "\r", "lf": "\n", "crlf": "\r\n"}
        payload = (text + endings[line_ending]).encode(self.encoding)
        return self.write_bytes(payload)

    def close(self) -> None:
        self._stop.set()
        port = self._serial
        if port is not None:
            cancel_read = getattr(port, "cancel_read", None)
            if callable(cancel_read):
                try:
                    cancel_read()
                except (SerialException, OSError):
                    pass
        if self._reader is not None and self._reader.is_alive():
            self._reader.join(timeout=max(1.0, self.read_timeout * 4))
        if port is not None and port.is_open:
            port.close()
        self._serial = None
        self.log.event(f"serial-close port={self.port!r}")
        self.log.close()

    def __enter__(self) -> "SerialMonitor":
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()


def available_ports() -> list[list_ports.ListPortInfo]:
    return sorted(list_ports.comports(), key=lambda item: item.device.casefold())


def print_ports(output: TextIO = sys.stdout) -> None:
    ports = available_ports()
    if not ports:
        output.write("No serial ports detected.\n")
        return
    for item in ports:
        output.write(
            f"{item.device}\t{item.description or '-'}\t{item.hwid or '-'}\n"
        )


def safe_port_name(port: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", port).strip("_")
    return name or "serial"


def default_log_path(port: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path(__file__).resolve().parent / "logs" / f"{safe_port_name(port)}-{stamp}.log"


def run_self_test() -> int:
    payload = b"ABW_SERIAL_MONITOR_SELF_TEST\r\n"
    with serial.serial_for_url("loop://", timeout=1.0, write_timeout=1.0) as loop:
        written = loop.write(payload)
        loop.flush()
        received = loop.read(len(payload))
    if written != len(payload) or received != payload:
        print(
            f"SELF-TEST FAIL: written={written}, received={received!r}",
            file=sys.stderr,
        )
        return 1

    # Verify that the default log is a readable receive stream, preserves the
    # target's exact newline form, and handles fragmented multibyte text.
    with tempfile.TemporaryDirectory(prefix="edgi-talk-serial-monitor-") as temp_dir:
        log_path = Path(temp_dir) / "readable.log"
        chinese = "串口".encode("utf-8")
        log = SerialLog(log_path, "utf-8", "replace", "text")
        log.event("must-not-appear")
        log.record("RX", b"line 1\r")
        log.record("RX", b"\nline 2\n")
        log.record("TX", b"must-not-appear\r\n")
        log.record("RX", chinese[:2])
        log.record("RX", chinese[2:])
        log.close()

        expected = b"line 1\r\nline 2\n" + chinese
        actual = log_path.read_bytes()
        if actual != expected:
            print(
                f"SELF-TEST FAIL: readable log={actual!r}, expected={expected!r}",
                file=sys.stderr,
            )
            return 1

        event_path = Path(temp_dir) / "events.log"
        event_log = SerialLog(event_path, "utf-8", "replace", "event")
        event_log.event("serial-open port='loop://' baud=115200")
        event_log.record("TX", b"version\r\n")
        event_log.record("RX", b"msh >")
        event_log.close()
        event_text = event_path.read_text(encoding="utf-8")
        required_event_fields = (
            "EVENT monitor-open",
            "EVENT serial-open",
            "TX bytes=",
            "RX bytes=",
            "EVENT monitor-close",
        )
        if not all(field in event_text for field in required_event_fields):
            print(
                "SELF-TEST FAIL: event log is missing required evidence fields",
                file=sys.stderr,
            )
            return 1

    print(
        "SELF-TEST PASS: pyserial loop:// TX/RX; readable log preserves "
        "CR/LF and fragmented UTF-8; event log preserves byte evidence"
    )
    return 0


def _stdin_worker(lines: queue.Queue[str | None]) -> None:
    while True:
        line = sys.stdin.readline()
        if line == "":
            lines.put(None)
            return
        lines.put(line.rstrip("\r\n"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Monitor and log an Edgi-Talk/RT-Thread serial console.",
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--list", action="store_true", help="list serial ports and exit")
    action.add_argument("--self-test", action="store_true", help="run loopback self-test and exit")

    parser.add_argument("--port", help="serial port, for example COM5 or loop://")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument("--encoding", default="utf-8")
    parser.add_argument(
        "--decode-errors",
        choices=("strict", "ignore", "replace", "backslashreplace"),
        default="replace",
    )
    parser.add_argument("--read-timeout", type=float, default=DEFAULT_READ_TIMEOUT)
    parser.add_argument("--write-timeout", type=float, default=DEFAULT_WRITE_TIMEOUT)
    parser.add_argument("--rtscts", action="store_true", help="enable RTS/CTS flow control")
    parser.add_argument("--xonxoff", action="store_true", help="enable XON/XOFF flow control")
    parser.add_argument("--timestamps", action="store_true", help="prefix displayed RX chunks")
    parser.add_argument(
        "--line-ending",
        choices=("none", "cr", "lf", "crlf"),
        default="crlf",
        help="line ending appended to interactive and --send commands",
    )
    parser.add_argument(
        "--send",
        action="append",
        default=[],
        metavar="TEXT",
        help="send text after opening; repeat for multiple commands",
    )
    parser.add_argument(
        "--send-delay",
        type=float,
        default=0.10,
        help="delay between repeated --send commands",
    )
    parser.add_argument("--no-input", action="store_true", help="disable stdin command input")
    parser.add_argument("--duration", type=float, help="stop after this many seconds")
    parser.add_argument("--log", type=Path, help="explicit log file")
    parser.add_argument(
        "--log-format",
        choices=LOG_FORMATS,
        default="text",
        help=(
            "text: readable RX stream preserving device newlines (default); "
            "event: timestamped RX/TX records with hexadecimal bytes"
        ),
    )
    parser.add_argument("--no-log", action="store_true", help="disable automatic log file")
    return parser


def run(args: argparse.Namespace) -> int:
    if args.list:
        print_ports()
        return 0
    if args.self_test:
        return run_self_test()
    if not args.port:
        print("error: --port is required unless --list or --self-test is used", file=sys.stderr)
        return 2
    if args.baud <= 0 or args.read_timeout < 0 or args.write_timeout <= 0:
        print("error: invalid baud or timeout", file=sys.stderr)
        return 2
    if args.duration is not None and args.duration <= 0:
        print("error: --duration must be greater than zero", file=sys.stderr)
        return 2

    log_path = None if args.no_log else (args.log or default_log_path(args.port))
    monitor = SerialMonitor(
        args.port,
        args.baud,
        encoding=args.encoding,
        decode_errors=args.decode_errors,
        read_timeout=args.read_timeout,
        write_timeout=args.write_timeout,
        rtscts=args.rtscts,
        xonxoff=args.xonxoff,
        timestamps=args.timestamps,
        log_path=log_path,
        log_format=args.log_format,
    )

    input_lines: queue.Queue[str | None] = queue.Queue()
    input_thread: threading.Thread | None = None
    started = time.monotonic()

    try:
        monitor.open()
        print(
            f"Opened {args.port} at {args.baud} baud"
            + (
                f"; log={log_path} ({args.log_format})"
                if log_path
                else "; logging disabled"
            ),
            file=sys.stderr,
        )
        for index, text in enumerate(args.send):
            monitor.write_text(text, args.line_ending)
            if index + 1 < len(args.send) and args.send_delay:
                time.sleep(args.send_delay)

        if not args.no_input:
            print(f"Enter commands line by line; enter {EXIT_COMMAND!r} to exit.", file=sys.stderr)
            input_thread = threading.Thread(
                target=_stdin_worker,
                args=(input_lines,),
                name="serial-stdin",
                daemon=True,
            )
            input_thread.start()

        while True:
            if monitor.reader_error is not None:
                raise monitor.reader_error
            if args.duration is not None and time.monotonic() - started >= args.duration:
                break
            if args.no_input:
                time.sleep(0.05)
                continue
            try:
                line = input_lines.get(timeout=0.05)
            except queue.Empty:
                continue
            if line is None or line == EXIT_COMMAND:
                break
            monitor.write_text(line, args.line_ending)
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
    except (SerialException, OSError, RuntimeError, UnicodeError) as exc:
        print(f"serial monitor error: {exc}", file=sys.stderr)
        return 1
    finally:
        monitor.close()

    return 0


def main() -> int:
    return run(build_parser().parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
