"""Compile the actual portable protocol code, not a Python reimplementation."""
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[3]
source = root / "projects/FeatherTalk_M55/applications/airplay2"
output = root / "tmp/test-airplay-protocol.exe"
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                "-I", str(source), str(source / "ft_airplay_protocol.c"),
                str(Path(__file__).with_suffix(".c")), "-o", str(output)], check=True)
subprocess.run([str(output)], check=True)
tests = Path(__file__).parent
output = root / "tmp/test-airplay-sink.exe"
subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                "-I", str(tests / "airplay_stubs"), "-I", str(source),
                "-I", str(source.parent), str(source / "ft_airplay_sink.c"),
                str(tests / "test_airplay_sink.c"), "-o", str(output)], check=True)
subprocess.run([str(output)], check=True)
