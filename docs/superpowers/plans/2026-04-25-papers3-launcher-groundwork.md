# PaperS3 Launcher Groundwork Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the first working slice of the multi-firmware architecture by adding the launcher/runtime partition layout, dual PaperS3 build targets, and raw firmware inspect/import tooling that can classify the two sample Burner exports already in the repo.

**Architecture:** Keep the current `paper-buddy` runtime behavior intact while introducing new infrastructure beside it. The first slice does not attempt the full launcher install UI yet; it adds the build skeleton and desktop tooling needed to turn raw exports into normalized package directories. The existing `papers3` flow stays usable during migration, while new `papers3_buddy` and `papers3_launcher` targets become the future path.

**Tech Stack:** PlatformIO, ESP32 Arduino firmware, Python 3 standard library, existing `esptool.py` and `gen_esp32part.py`, `unittest`

---

## Scope Split

The approved design spec covers multiple independent subsystems. This plan intentionally covers only the first executable sub-project:

1. add the new partition layout and PaperS3 build skeleton
2. add raw firmware inspection tooling
3. add raw firmware import/package generation

Follow-up plans should handle:

1. launcher install UX and on-device package browsing
2. runtime boot/recovery policy hardening
3. plugin command/workflow migration to launcher-aware flashing

## File Map

### Files to Create

- `partitions-papers3-launcher.csv`
- `src/apps/launcher/main.cpp`
- `tests/test_platform_layout.py`
- `tools/firmware_inspect.py`
- `tests/test_firmware_inspect.py`
- `tools/firmware_import.py`
- `tests/test_firmware_import.py`

### Files to Modify

- `platformio.ini`
- `README.md`
- `README.en.md`

### Existing Fixtures Used by Tests

- `firmware/esp32fw.bin`
- `firmware/d07ae7625d1e15309886e9e884767ff7.bin`

## Task 1: Add the Launcher/Runtime Build Skeleton

**Files:**
- Create: `tests/test_platform_layout.py`
- Create: `partitions-papers3-launcher.csv`
- Create: `src/apps/launcher/main.cpp`
- Modify: `platformio.ini`
- Modify: `README.md`
- Modify: `README.en.md`

- [ ] **Step 1: Write the failing layout/build-shape test**

```python
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class PlatformLayoutTests(unittest.TestCase):
    def test_platformio_declares_launcher_and_buddy_envs(self):
        text = (REPO_ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("[env:papers3_buddy]", text)
        self.assertIn("[env:papers3_launcher]", text)

    def test_launcher_partition_csv_matches_phase1_layout(self):
        csv_path = REPO_ROOT / "partitions-papers3-launcher.csv"
        self.assertTrue(csv_path.exists(), f"missing {csv_path}")
        text = csv_path.read_text(encoding="utf-8")
        self.assertIn("launcher,app,factory,0x20000,1M,", text)
        self.assertIn("runtime,app,ota_0,0x120000,0xDF0000,", text)
        self.assertIn("storage,data,spiffs,0xF10000,0x0E0000,", text)
        self.assertIn("coredump,data,coredump,0xFF0000,0x010000,", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
python3 -m unittest tests/test_platform_layout.py -v
```

Expected:

```text
FAIL: test_platformio_declares_launcher_and_buddy_envs
FAIL: test_launcher_partition_csv_matches_phase1_layout
```

- [ ] **Step 3: Add the new partition CSV and launcher firmware stub**

Create `partitions-papers3-launcher.csv`:

```csv
# Phase-1 launcher/runtime layout for PaperS3 (16MB flash).
# Name,     Type, SubType,  Offset,   Size,      Flags
nvs,        data, nvs,      0x9000,   24K,
otadata,    data, ota,      0xF000,   8K,
phy_init,   data, phy,      0x11000,  4K,
launcher,   app,  factory,  0x20000,  1M,
runtime,    app,  ota_0,    0x120000, 0xDF0000,
storage,    data, spiffs,   0xF10000, 0x0E0000,
coredump,   data, coredump, 0xFF0000, 0x010000,
```

Create `src/apps/launcher/main.cpp`:

```cpp
#include <Arduino.h>
#include <M5Unified.h>

namespace {
constexpr const char* kLauncherTitle = "PaperS3 Launcher";
constexpr const char* kLauncherSubtitle = "Groundwork build";

void drawLauncherSplash() {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(kLauncherTitle, M5.Display.width() / 2, M5.Display.height() / 2 - 20);
  M5.Display.setTextSize(1);
  M5.Display.drawString(kLauncherSubtitle, M5.Display.width() / 2, M5.Display.height() / 2 + 20);
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = false;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.fallback_board = m5::board_t::board_M5PaperS3;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setEpdMode(m5gfx::epd_quality);
  drawLauncherSplash();
  M5.Display.display();
  Serial.println("[launcher] booted");
}

void loop() {
  M5.update();
  delay(20);
}
```

- [ ] **Step 4: Add new PlatformIO envs while keeping the legacy one available**

Modify `platformio.ini` by adding these sections below the existing `papers3` env:

```ini
[env:papers3_buddy]
platform = espressif32
board = esp32-s3-devkitm-1
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
board_build.partitions = partitions-papers3-launcher.csv
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.arduino.memory_type = qio_opi
build_flags =
	-DESP32S3
	-DBOARD_HAS_PSRAM
	-DCORE_DEBUG_LEVEL=0
	-DARDUINO_USB_CDC_ON_BOOT=1
	-DARDUINO_USB_MODE=1
	-DBUDDY_DEVICE_LABEL=\"PaperS3\"
	-DBUDDY_PIO_ENV=\"papers3_buddy\"
	-DBUDDY_TARGET_PAPERS3=1
	-DBUDDY_LANDSCAPE=1
build_src_filter = -<*> +<ble_bridge.cpp> +<paper/>
lib_deps =
	https://github.com/vroland/epdiy.git#d84d26ebebd780c4c9d4218d76fbe2727ee42b47
	https://github.com/m5stack/M5Unified
	bblanchon/ArduinoJson @ ^7.0.0
platform_packages = platformio/tool-esptoolpy@^2.41100.0

[env:papers3_launcher]
platform = espressif32
board = esp32-s3-devkitm-1
framework = arduino
monitor_speed = 115200
board_build.partitions = partitions-papers3-launcher.csv
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.arduino.memory_type = qio_opi
build_flags =
	-DESP32S3
	-DCORE_DEBUG_LEVEL=0
	-DARDUINO_USB_CDC_ON_BOOT=1
	-DARDUINO_USB_MODE=1
	-DLAUNCHER_TARGET_PAPERS3=1
build_src_filter = -<*> +<apps/launcher/>
lib_deps =
	https://github.com/m5stack/M5Unified
platform_packages = platformio/tool-esptoolpy@^2.41100.0
```

Leave `[env:papers3]` in place for compatibility during migration.

- [ ] **Step 5: Document the new build targets without changing the default user path yet**

Add this short section to both `README.md` and `README.en.md` near the development/build area:

```md
### Multi-firmware groundwork

The repository now has two new PaperS3 build targets:

- `papers3_buddy` — the existing buddy runtime on the new launcher/runtime partition map
- `papers3_launcher` — a minimal launcher firmware stub used to verify the new flash layout

The legacy `papers3` target remains available during the migration.
```

- [ ] **Step 6: Run the tests and both build targets**

Run:

```bash
python3 -m unittest tests/test_platform_layout.py -v
pio run -e papers3_launcher
pio run -e papers3_buddy
```

Expected:

```text
OK
...
Environment    Status    Duration
papers3_launcher    SUCCESS
papers3_buddy       SUCCESS
```

- [ ] **Step 7: Commit**

```bash
git add tests/test_platform_layout.py partitions-papers3-launcher.csv src/apps/launcher/main.cpp platformio.ini README.md README.en.md
git commit -m "feat: add papers3 launcher groundwork"
```

## Task 2: Add Raw Firmware Inspection Tooling

**Files:**
- Create: `tools/firmware_inspect.py`
- Create: `tests/test_firmware_inspect.py`

- [ ] **Step 1: Write the failing inspection tests using the existing sample binaries**

Create `tests/test_firmware_inspect.py`:

```python
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
INSPECT = REPO_ROOT / "tools" / "firmware_inspect.py"
FULL_FLASH = REPO_ROOT / "firmware" / "esp32fw.bin"
RUNTIME_SAMPLE = REPO_ROOT / "firmware" / "d07ae7625d1e15309886e9e884767ff7.bin"


def inspect_json(path: Path) -> dict:
    raw = subprocess.check_output(
        ["python3", str(INSPECT), "--json", str(path)],
        text=True,
        cwd=str(REPO_ROOT),
    )
    return json.loads(raw)


class FirmwareInspectTests(unittest.TestCase):
    def test_full_flash_sample_is_reported_as_full_flash_image(self):
        report = inspect_json(FULL_FLASH)
        self.assertEqual(report["chip"], "esp32s3")
        self.assertEqual(report["image_kind"], "full-flash")
        self.assertEqual(report["partition_table"]["app0"]["offset"], "0x10000")
        self.assertEqual(report["partition_table"]["spiffs"]["offset"], "0x310000")

    def test_runtime_sample_exposes_runtime_candidate_app(self):
        report = inspect_json(RUNTIME_SAMPLE)
        self.assertEqual(report["chip"], "esp32s3")
        self.assertEqual(report["image_kind"], "flash-export")
        self.assertEqual(report["partition_table"]["app0"]["size"], "15M")
        self.assertTrue(report["runtime_candidate"]["valid"])
        self.assertEqual(report["runtime_candidate"]["offset"], "0x10000")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```bash
python3 -m unittest tests/test_firmware_inspect.py -v
```

Expected:

```text
python3: can't open file '.../tools/firmware_inspect.py'
```

- [ ] **Step 3: Implement the inspection CLI**

Create `tools/firmware_inspect.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GEN_PART = REPO_ROOT / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "gen_esp32part.py"
if not GEN_PART.exists():
    GEN_PART = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "gen_esp32part.py"
ESPTOOL = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"


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
    app_path = image_path.with_suffix(".runtime.tmp.bin")
    data = image_path.read_bytes()
    if len(data) <= app_offset:
        return {"valid": False, "offset": "0x10000"}
    app_path.write_bytes(data[app_offset:])
    try:
        info = read_esptool_info(app_path)
        valid = "Validation hash:" in info
    finally:
        app_path.unlink(missing_ok=True)
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
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests to verify they pass**

Run:

```bash
python3 -m unittest tests/test_firmware_inspect.py -v
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add tools/firmware_inspect.py tests/test_firmware_inspect.py
git commit -m "feat: add firmware inspection cli"
```

## Task 3: Add Raw Firmware Import and Package Generation

**Files:**
- Create: `tools/firmware_import.py`
- Create: `tests/test_firmware_import.py`

- [ ] **Step 1: Write the failing import tests**

Create `tests/test_firmware_import.py`:

```python
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
IMPORT = REPO_ROOT / "tools" / "firmware_import.py"
FULL_FLASH = REPO_ROOT / "firmware" / "esp32fw.bin"
RUNTIME_SAMPLE = REPO_ROOT / "firmware" / "d07ae7625d1e15309886e9e884767ff7.bin"


def run_import(image: Path, package_id: str, out_dir: Path) -> Path:
    subprocess.check_call(
        ["python3", str(IMPORT), "--id", package_id, "--out", str(out_dir), str(image)],
        cwd=str(REPO_ROOT),
    )
    return out_dir / package_id


class FirmwareImportTests(unittest.TestCase):
    def test_full_flash_image_is_marked_full_flash_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            package_dir = run_import(FULL_FLASH, "sample-full", Path(tmp))
            manifest = json.loads((package_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["compatibility"], "full-flash-only")
            self.assertEqual(manifest["install_mode"], "host-flash")

    def test_runtime_candidate_image_is_packaged_as_switchable(self):
        with tempfile.TemporaryDirectory() as tmp:
            package_dir = run_import(RUNTIME_SAMPLE, "sample-runtime", Path(tmp))
            manifest = json.loads((package_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["compatibility"], "switchable")
            self.assertEqual(manifest["install_mode"], "runtime-slot")
            self.assertTrue((package_dir / "app.bin").exists())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```bash
python3 -m unittest tests/test_firmware_import.py -v
```

Expected:

```text
python3: can't open file '.../tools/firmware_import.py'
```

- [ ] **Step 3: Implement the importer and package manifest writer**

Create `tools/firmware_import.py`:

```python
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


def build_manifest(package_id: str, image_path: Path, report: dict[str, object]) -> dict[str, object]:
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run:

```bash
PYTHONPATH=tools python3 -m unittest tests/test_firmware_import.py -v
```

Expected:

```text
OK
```

- [ ] **Step 5: Commit**

```bash
git add tools/firmware_import.py tests/test_firmware_import.py
git commit -m "feat: add firmware import packaging cli"
```

## Task 4: Re-run the Groundwork Verification Matrix

**Files:**
- Test only: `tests/test_platform_layout.py`
- Test only: `tests/test_firmware_inspect.py`
- Test only: `tests/test_firmware_import.py`

- [ ] **Step 1: Run the full Python test suite**

Run:

```bash
PYTHONPATH=tools python3 -m unittest discover -s tests -v
```

Expected:

```text
...
Ran X tests in Y.YYYs

OK
```

- [ ] **Step 2: Re-run both new PaperS3 build targets**

Run:

```bash
pio run -e papers3_launcher
pio run -e papers3_buddy
```

Expected:

```text
SUCCESS
SUCCESS
```

- [ ] **Step 3: Confirm the importer generates both expected compatibility classes**

Run:

```bash
rm -rf /tmp/papers3-packages
mkdir -p /tmp/papers3-packages
python3 tools/firmware_import.py --id esp32fw --out /tmp/papers3-packages firmware/esp32fw.bin
python3 tools/firmware_import.py --id runtime --out /tmp/papers3-packages firmware/d07ae7625d1e15309886e9e884767ff7.bin
cat /tmp/papers3-packages/esp32fw/manifest.json
cat /tmp/papers3-packages/runtime/manifest.json
```

Expected:

```text
"compatibility": "full-flash-only"
...
"compatibility": "switchable"
```

- [ ] **Step 4: Commit the final verification-only checkpoint if any fixups were needed**

```bash
git status --short
```

Expected:

```text
```

If clean, no extra commit is required.

## Self-Review Notes

Spec coverage for this slice:

- launcher/runtime partition map: covered by Task 1
- dual build targets: covered by Task 1
- raw firmware inspection: covered by Task 2
- compatibility classification and package generation: covered by Task 3

Intentional gaps left for later plans:

- launcher install UI
- on-device package browser
- plugin command migration
- recovery boot policy hardening
