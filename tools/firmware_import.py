#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from firmware_inspect import inspect_image


def classify(report: dict[str, object]) -> tuple[str, str]:
    runtime = report["runtime_candidate"]
    partitions = report["partition_table"]
    spiffs = partitions.get("spiffs", {})

    if report["image_kind"] == "full-flash" and spiffs.get("size") == "13184K":
        return ("full-flash-only", "host-flash")
    if runtime["valid"]:
        return ("switchable", "runtime-slot")
    return ("unsupported", "none")


def extract_runtime_app(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    dst.write_bytes(data[0x10000:])


def build_manifest(
    package_id: str, image_path: Path, report: dict[str, object]
) -> dict[str, object]:
    compatibility, install_mode = classify(report)
    return {
        "id": package_id,
        "name": package_id,
        "source_type": "burner-export",
        "chip": report["chip"],
        "flash_size": 16777216,
        "compatibility": compatibility,
        "install_mode": install_mode,
        "runtime": {
            "app_offset": 0,
            "app_file": "app.bin" if compatibility == "switchable" else None,
            "data_file": None,
        },
        "source_image": image_path.name,
    }


def import_image(image_path: Path, out_root: Path, package_id: str) -> Path:
    report = inspect_image(image_path)
    package_dir = out_root / package_id
    original_dir = package_dir / "original"
    original_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(image_path, original_dir / image_path.name)

    manifest = build_manifest(package_id, image_path, report)
    if manifest["compatibility"] == "switchable":
        extract_runtime_app(image_path, package_dir / "app.bin")

    (package_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return package_dir


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("image", type=Path)
    args = ap.parse_args()

    package_dir = import_image(args.image, args.out, args.id)
    print(package_dir)


if __name__ == "__main__":
    main()
