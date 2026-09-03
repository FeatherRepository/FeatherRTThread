#!/usr/bin/env python3
"""Run the FeatherTalk on-board full-frame benchmark in every visual scene."""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from pathlib import Path

import serial


SCENES = [
    (0, "home", 0.8),
    (1, "search", 0.8),
    (2, "system", 0.8),
    (3, "settings", 0.8),
    (4, "media", 0.8),
    (5, "recorder", 0.8),
    (6, "gallery", 2.5),
    (7, "files", 1.2),
    (8, "about", 0.8),
    (9, "settings-display", 0.8),
    (10, "settings-audio", 0.8),
    (11, "settings-wifi", 0.8),
    (12, "settings-bluetooth", 0.8),
    (13, "settings-storage", 1.2),
    (14, "settings-usb", 1.2),
    (15, "settings-time-language", 0.8),
    (16, "settings-personalization", 0.8),
    (17, "all-apps", 0.8),
    (18, "shade-open", 1.0),
    (19, "shade-drag", 0.8),
    (20, "search-keyboard", 0.8),
    (21, "settings-keyboard", 0.8),
    (22, "tile-edit", 1.0),
    (23, "gallery-viewer", 3.0),
    (24, "files-action", 1.2),
    (25, "media-playing", 1.0),
    (26, "media-folder", 1.0),
    (27, "alert", 0.8),
    (28, "settings-audio-output", 0.8),
    (29, "settings-audio-input", 0.8),
]

COMPLETE_RE = re.compile(
    r"complete requested=(?P<requested>\d+) rendered=(?P<rendered>\d+) "
    r"elapsed=(?P<elapsed_ms>\d+)ms fps=(?P<fps>\d+\.\d+) "
    r"batch=(?P<batch>\d+) submits=(?P<submits>\d+) "
    r"collect=(?P<collect_us>\d+)us encode=(?P<encode_us>\d+)us "
    r"finish=(?P<finish_us>\d+)us retire-gpu=(?P<gpu_wait_us>\d+)us "
    r"scanout=(?P<scanout_wait_us>\d+)us drain=(?P<drain_ms>\d+)ms "
    r"gpu-busy=(?P<gpu_busy_pct>\d+\.\d+)% jobs=(?P<jobs>\d+)"
)
TASK_RE = re.compile(
    r"fill=(?P<fill_us>\d+) border=(?P<border_us>\d+) "
    r"shadow=(?P<shadow_us>\d+) label=(?P<label_us>\d+) "
    r"image=(?P<image_us>\d+) layer=(?P<layer_us>\d+) "
    r"line=(?P<line_us>\d+) arc=(?P<arc_us>\d+) "
    r"triangle=(?P<triangle_us>\d+) mask=(?P<mask_us>\d+) "
    r"vector=(?P<vector_us>\d+)"
)
LABEL_RE = re.compile(
    r"glyph-draw=(?P<glyph_draw_us>\d+)us "
    r"layout/font=(?P<label_layout_us>\d+)us glyphs/frame=(?P<glyphs_per_frame>\d+)"
)
SETUP_RE = re.compile(r"setup=(?P<setup_ms>\d+)ms result=(?P<setup_result>-?\d+)")


class Monitor:
    def __init__(self, port: str, baud: int, raw_log: Path) -> None:
        self.serial = serial.Serial(port, baudrate=baud, timeout=0.1)
        self.log = raw_log.open("w", encoding="utf-8", newline="")
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.log.close()
        self.serial.close()

    def send(self, command: str) -> None:
        line = f">>> {command}\n"
        sys.stdout.write(line)
        self.log.write(line)
        self.log.flush()
        self.serial.write((command + "\r\n").encode("utf-8"))
        self.serial.flush()

    def read_until(self, marker: str, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        chunks: list[str] = []
        joined = ""
        while time.monotonic() < deadline:
            data = self.serial.read(self.serial.in_waiting or 1)
            if not data:
                continue
            text = data.decode("utf-8", errors="replace")
            chunks.append(text)
            joined += text
            sys.stdout.write(text)
            sys.stdout.flush()
            self.log.write(text)
            self.log.flush()
            marker_at = joined.find(marker)
            if marker_at >= 0 and "\n" in joined[marker_at:]:
                return joined
        raise TimeoutError(f"timeout waiting for {marker!r}")


def parse_selection(value: str | None) -> list[tuple[int, str, float]]:
    if not value:
        return SCENES
    selected = {int(item.strip()) for item in value.split(",") if item.strip()}
    unknown = selected.difference(scene[0] for scene in SCENES)
    if unknown:
        raise ValueError(f"unknown scene IDs: {sorted(unknown)}")
    return [scene for scene in SCENES if scene[0] in selected]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--scenes", help="comma-separated scene IDs; default is all")
    parser.add_argument("--output", default="tools/freather/logs/ui-scene-benchmark")
    args = parser.parse_args()

    scenes = parse_selection(args.scenes)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    raw_log = output.with_suffix(".log")
    csv_path = output.with_suffix(".csv")
    monitor = Monitor(args.port, args.baud, raw_log)
    rows: list[dict[str, str | int | float]] = []
    failures: list[str] = []
    batching_warnings: list[str] = []

    try:
        for scene_id, scene_name, settle_s in scenes:
            print(f"\n=== scene {scene_id}: {scene_name} ===")
            try:
                monitor.send(f"feather_ui_scene {scene_id}")
                setup_text = monitor.read_until(
                    f"[UI-SCENE] ready id={scene_id} name={scene_name}", 12.0
                )
                setup_match = SETUP_RE.search(setup_text)
                if setup_match is None or int(setup_match.group("setup_result")) != 0:
                    raise RuntimeError("scene setup failed")
                time.sleep(settle_s)
                monitor.send("feather_ui_bench")
                combined = monitor.read_until("[UI-BENCH] label-split", 18.0)
                complete = COMPLETE_RE.search(combined)
                tasks = TASK_RE.search(combined)
                label = LABEL_RE.search(combined)
                if complete is None or tasks is None or label is None:
                    raise RuntimeError("incomplete benchmark record")
                row: dict[str, str | int | float] = {
                    "scene_id": scene_id,
                    "scene": scene_name,
                    "setup_ms": int(setup_match.group("setup_ms")),
                }
                for key, value in complete.groupdict().items():
                    row[key] = float(value) if "." in value else int(value)
                for match in (tasks, label):
                    row.update({key: int(value) for key, value in match.groupdict().items()})
                rows.append(row)
                if not (
                    row["rendered"] == row["requested"]
                    and row["batch"] == row["requested"]
                    and row["jobs"] == row["submits"]
                ):
                    failures.append(f"{scene_name}: render/batch/job completion contract")
                elif row["submits"] != row["requested"]:
                    batching_warnings.append(
                        f"{scene_name}: {row['submits']} submits for "
                        f"{row['requested']} frames "
                        f"({float(row['submits']) / float(row['requested']):.1f}/frame)"
                    )
            except Exception as error:  # Continue so one bad page does not hide the rest.
                failures.append(f"{scene_name}: {error}")
                print(f"\nERROR: {scene_name}: {error}", file=sys.stderr)
        monitor.send("feather_ui_scene 0")
        monitor.read_until("[UI-SCENE] ready id=0 name=home", 12.0)
        time.sleep(0.8)
        monitor.send("feather_ui_status")
        monitor.read_until("scanout:", 8.0)
    finally:
        monitor.close()

    if rows:
        fields = list(rows[0].keys())
        with csv_path.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        print("\n=== FPS ranking (slowest first) ===")
        for row in sorted(rows, key=lambda item: float(item["fps"])):
            print(
                f"{row['scene_id']:>2} {row['scene']:<26} {row['fps']:>6} FPS "
                f"collect={row['collect_us']:>5}us encode={row['encode_us']:>5}us "
                f"GPU={row['gpu_busy_pct']:>5}%"
            )
    print(f"raw log: {raw_log}")
    print(f"CSV: {csv_path}")
    if batching_warnings:
        print("Batching warnings:")
        for warning in batching_warnings:
            print(f"- {warning}")
    if failures:
        print("Failures:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
