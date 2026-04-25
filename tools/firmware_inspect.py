#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def find_tool(*parts: str) -> Path:
    candidates = [
        REPO_ROOT / ".platformio" / "packages" / Path(*parts),
        Path.home() / ".platformio" / "packages" / Path(*parts),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(f"tool not found: {'/'.join(parts)}")


GEN_PART = find_tool("framework-arduinoespressif32", "tools", "gen_esp32part.py")
ESPTOOL = find_tool("tool-esptoolpy", "esptool.py")


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True)


def parse_partition_table(image_path: Path) -> dict[str, dict[str, str]]:
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        with image_path.open("rb") as src:
            src.seek(0x8000)
            tmp.write(src.read(0xC00))
            tmp.flush()
        text = run(["python3", str(GEN_PART), tmp.name])

    rows: dict[str, dict[str, str]] = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        name, part_type, subtype, offset, size, *_ = [cell.strip() for cell in line.split(",")]
        rows[name] = {
            "type": part_type,
            "subtype": subtype,
            "offset": offset,
            "size": size,
        }
    return rows


def read_esptool_info(image_path: Path) -> str:
    return run(["python3", str(ESPTOOL), "image_info", "--version", "2", str(image_path)])


def detect_chip(esptool_text: str) -> str:
    if "ESP32-S3" in esptool_text:
        return "esp32s3"
    return "unknown"


def build_runtime_candidate(image_path: Path) -> dict[str, object]:
    app_offset = 0x10000
    data = image_path.read_bytes()
    if len(data) <= app_offset:
        return {"valid": False, "offset": "0x10000"}

    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        tmp.write(data[app_offset:])
        tmp.flush()
        try:
            info = read_esptool_info(Path(tmp.name))
            valid = "Validation hash:" in info
        except subprocess.CalledProcessError:
            valid = False

    return {
        "valid": valid,
        "offset": "0x10000",
    }


def inspect_image(image_path: Path) -> dict[str, object]:
    esptool_text = read_esptool_info(image_path)
    partitions = parse_partition_table(image_path)
    size = image_path.stat().st_size
    image_kind = "full-flash" if size == 16 * 1024 * 1024 else "flash-export"
    return {
        "path": str(image_path),
        "size": size,
        "chip": detect_chip(esptool_text),
        "image_kind": image_kind,
        "partition_table": partitions,
        "runtime_candidate": build_runtime_candidate(image_path),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("image", type=Path)
    args = ap.parse_args()

    report = inspect_image(args.image)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
