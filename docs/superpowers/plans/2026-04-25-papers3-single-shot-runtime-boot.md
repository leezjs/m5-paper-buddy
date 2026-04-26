# PaperS3 Single-Shot Runtime Boot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make PaperS3 runtime launches one-shot so any reset, crash, watchdog restart, or power cycle after entering runtime returns to the factory launcher.

**Architecture:** Add a small raw `bootcfg` partition and a shared fixed-format boot request record. The launcher writes `BOOT_RUNTIME_ONCE`; a custom ESP-IDF bootloader hook consumes that request, writes OTA data for `runtime` once, and otherwise erases `otadata` so the stock bootloader falls back to the factory `launcher`.

**Tech Stack:** PlatformIO, ESP32 Arduino, ESP-IDF bootloader hooks, Python `unittest`

---

## File Map

### Files To Create

- `include/papers3_bootcfg.h`: C-compatible bootcfg format, constants, CRC helpers, and record constructors shared by launcher and bootloader hook.
- `src/apps/launcher/bootcfg.h`: launcher-local API for writing the one-shot runtime request.
- `src/apps/launcher/bootcfg.cpp`: ESP partition implementation for writing `bootcfg`.
- `bootloader/papers3_single_shot/CMakeLists.txt`: minimal ESP-IDF project wrapper for producing the custom bootloader.
- `bootloader/papers3_single_shot/main/CMakeLists.txt`: minimal app target required by ESP-IDF project shape.
- `bootloader/papers3_single_shot/main/placeholder.c`: no-op app source; only the bootloader artifact is used.
- `bootloader/papers3_single_shot/bootloader_components/papers3_boot_policy/CMakeLists.txt`: bootloader component declaration.
- `bootloader/papers3_single_shot/bootloader_components/papers3_boot_policy/papers3_boot_policy.c`: bootloader hook that maps bootcfg to OTA data.
- `bootloader/papers3_single_shot/partitions.csv`: bootloader project copy of the PaperS3 launcher partition map.
- `bootloader/papers3_single_shot/sdkconfig.defaults`: ESP32-S3 bootloader build defaults.
- `tools/papers3_custom_bootloader.py`: PlatformIO extra script that replaces the flashed bootloader image when a generated custom bootloader exists.
- `tests/test_papers3_single_shot_boot.py`: source-level regression tests for bootcfg, launcher handoff, and bootloader integration.

### Files To Modify

- `partitions-papers3-launcher.csv`: add `bootcfg,data,0x40,0x12000,4K`.
- `platformio.ini`: add the extra script to PaperS3 launcher/runtime envs and expose the shared include path.
- `src/apps/launcher/main.cpp`: write `BOOT_RUNTIME_ONCE` before rebooting runtime; remove long-term runtime boot targeting from normal launch/install paths.
- `docs/superpowers/specs/2026-04-25-papers3-single-shot-runtime-boot-design.md`: note that implementation uses a bootloader hook and stock app selection instead of replacing all bootloader selection code.

## Task 1: Add Failing Source And Layout Tests

**Files:**
- Modify: `tests/test_platform_layout.py`
- Create: `tests/test_papers3_single_shot_boot.py`

- [ ] **Step 1: Extend the partition layout test**

Add assertions that `partitions-papers3-launcher.csv` contains:

```python
self.assertIn("bootcfg,data,0x40,0x12000,4K,", text)
self.assertLess(text.index("bootcfg,data,0x40,0x12000,4K,"), text.index("launcher,app,factory,0x20000,1M,"))
```

- [ ] **Step 2: Add source-shape tests for bootcfg and bootloader hook**

Create `tests/test_papers3_single_shot_boot.py` with tests that assert:

```python
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BOOTCFG_HEADER = REPO_ROOT / "include" / "papers3_bootcfg.h"
LAUNCHER_SOURCE = REPO_ROOT / "src" / "apps" / "launcher" / "main.cpp"
BOOTLOADER_HOOK = (
    REPO_ROOT
    / "bootloader"
    / "papers3_single_shot"
    / "bootloader_components"
    / "papers3_boot_policy"
    / "papers3_boot_policy.c"
)
PLATFORMIO = REPO_ROOT / "platformio.ini"


class PaperS3SingleShotBootTests(unittest.TestCase):
    def test_shared_bootcfg_contract_defines_single_shot_record(self):
        text = BOOTCFG_HEADER.read_text(encoding="utf-8")
        self.assertIn("PAPERS3_BOOTCFG_PARTITION_LABEL", text)
        self.assertIn("PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE", text)
        self.assertIn("papers3_bootcfg_make_runtime_once", text)
        self.assertIn("papers3_bootcfg_is_runtime_once", text)
        self.assertIn("papers3_bootcfg_crc32", text)

    def test_launcher_writes_bootcfg_instead_of_persistent_runtime_target(self):
        text = LAUNCHER_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "bootcfg.h"', text)
        self.assertIn("writeRuntimeOnceBootRequest()", text)
        self.assertNotIn("esp_ota_set_boot_partition(runtime)", text)

    def test_custom_bootloader_hook_consumes_request_and_defaults_launcher(self):
        text = BOOTLOADER_HOOK.read_text(encoding="utf-8")
        self.assertIn("bootloader_after_init", text)
        self.assertIn("PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE", text)
        self.assertIn("write_runtime_otadata_once", text)
        self.assertIn("erase_otadata", text)
        self.assertIn("consume_bootcfg", text)

    def test_platformio_uses_custom_bootloader_script_for_papers3_launcher_layout(self):
        text = PLATFORMIO.read_text(encoding="utf-8")
        self.assertIn("tools/papers3_custom_bootloader.py", text)
        self.assertIn("PAPERS3_SINGLE_SHOT_BOOT=1", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run tests and verify they fail for missing behavior**

Run:

```bash
python3 -m unittest tests/test_platform_layout.py tests/test_papers3_single_shot_boot.py -v
```

Expected: failures for missing `bootcfg`, missing shared header, missing launcher include, and missing bootloader hook.

## Task 2: Add Shared Bootcfg Format

**Files:**
- Create: `include/papers3_bootcfg.h`

- [ ] **Step 1: Implement the fixed bootcfg record**

Add a C-compatible header with:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PAPERS3_BOOTCFG_PARTITION_LABEL "bootcfg"
#define PAPERS3_BOOTCFG_PARTITION_OFFSET 0x12000u
#define PAPERS3_BOOTCFG_PARTITION_SIZE 0x1000u
#define PAPERS3_OTADATA_OFFSET 0xE000u
#define PAPERS3_OTADATA_SIZE 0x2000u
#define PAPERS3_BOOTCFG_MAGIC 0x33534250u
#define PAPERS3_BOOTCFG_VERSION 1u
#define PAPERS3_BOOTCFG_COMMAND_NONE 0u
#define PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE 1u

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t command;
  uint32_t generation;
  uint32_t crc32;
} papers3_bootcfg_record_t;

static inline uint32_t papers3_bootcfg_crc32_bytes(const uint8_t* data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static inline uint32_t papers3_bootcfg_crc32(const papers3_bootcfg_record_t* record) {
  return papers3_bootcfg_crc32_bytes((const uint8_t*)record, 16u);
}

static inline bool papers3_bootcfg_is_valid(const papers3_bootcfg_record_t* record) {
  return record->magic == PAPERS3_BOOTCFG_MAGIC &&
         record->version == PAPERS3_BOOTCFG_VERSION &&
         record->crc32 == papers3_bootcfg_crc32(record);
}

static inline bool papers3_bootcfg_is_runtime_once(const papers3_bootcfg_record_t* record) {
  return papers3_bootcfg_is_valid(record) &&
         record->command == PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE;
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make(uint32_t command, uint32_t generation) {
  papers3_bootcfg_record_t record = {
      PAPERS3_BOOTCFG_MAGIC,
      PAPERS3_BOOTCFG_VERSION,
      command,
      generation,
      0u,
  };
  record.crc32 = papers3_bootcfg_crc32(&record);
  return record;
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make_runtime_once(uint32_t generation) {
  return papers3_bootcfg_make(PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE, generation);
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make_empty(uint32_t generation) {
  return papers3_bootcfg_make(PAPERS3_BOOTCFG_COMMAND_NONE, generation);
}
```

- [ ] **Step 2: Run the focused tests**

Run:

```bash
python3 -m unittest tests/test_papers3_single_shot_boot.py -v
```

Expected: bootcfg contract test passes; launcher and bootloader hook tests still fail.

## Task 3: Add Launcher Bootcfg Writer

**Files:**
- Create: `src/apps/launcher/bootcfg.h`
- Create: `src/apps/launcher/bootcfg.cpp`
- Modify: `src/apps/launcher/main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Add launcher API**

Create `src/apps/launcher/bootcfg.h`:

```cpp
#pragma once

#include <esp_err.h>

esp_err_t writeRuntimeOnceBootRequest();
```

- [ ] **Step 2: Implement ESP partition writer**

Create `src/apps/launcher/bootcfg.cpp`:

```cpp
#include "bootcfg.h"

#include <esp_partition.h>
#include <papers3_bootcfg.h>

esp_err_t writeRuntimeOnceBootRequest() {
  const esp_partition_t* bootcfg = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
      PAPERS3_BOOTCFG_PARTITION_LABEL);
  if (bootcfg == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }

  papers3_bootcfg_record_t current = {};
  uint32_t nextGeneration = 1;
  if (esp_partition_read(bootcfg, 0, &current, sizeof(current)) == ESP_OK &&
      papers3_bootcfg_is_valid(&current)) {
    nextGeneration = current.generation + 1;
  }

  papers3_bootcfg_record_t request =
      papers3_bootcfg_make_runtime_once(nextGeneration);
  esp_err_t rc = esp_partition_erase_range(bootcfg, 0, bootcfg->size);
  if (rc != ESP_OK) {
    return rc;
  }
  return esp_partition_write(bootcfg, 0, &request, sizeof(request));
}
```

- [ ] **Step 3: Include bootcfg and use it in runtime launch paths**

Modify `src/apps/launcher/main.cpp`:

```cpp
#include "bootcfg.h"
```

In `bootRuntime()`, replace `esp_ota_set_boot_partition(runtime)` with:

```cpp
esp_err_t rc = writeRuntimeOnceBootRequest();
if (rc != ESP_OK) {
  char msg[96];
  snprintf(msg, sizeof(msg), "Boot request failed: 0x%x",
           static_cast<unsigned>(rc));
  redrawWithStatus(msg);
  return;
}
```

In `installSelectedPackage()`, after image verification, replace the boot target switch with the same `writeRuntimeOnceBootRequest()` block.

- [ ] **Step 4: Add shared include path and feature flag**

Modify `platformio.ini` for `papers3_buddy` and `papers3_launcher`:

```ini
	-Iinclude
	-DPAPERS3_SINGLE_SHOT_BOOT=1
```

- [ ] **Step 5: Run focused tests**

Run:

```bash
python3 -m unittest tests/test_papers3_single_shot_boot.py -v
```

Expected: launcher test passes; bootloader and PlatformIO custom bootloader checks still fail until the next task.

## Task 4: Add Bootcfg Partition

**Files:**
- Modify: `partitions-papers3-launcher.csv`
- Create: `bootloader/papers3_single_shot/partitions.csv`

- [ ] **Step 1: Add bootcfg to the app partition layout**

Insert after `phy_init`:

```csv
bootcfg,data,0x40,0x12000,4K,
```

- [ ] **Step 2: Create bootloader project partition copy**

Create `bootloader/papers3_single_shot/partitions.csv` with the same contents as `partitions-papers3-launcher.csv`.

- [ ] **Step 3: Run the partition tests**

Run:

```bash
python3 -m unittest tests/test_platform_layout.py -v
```

Expected: all partition tests pass.

## Task 5: Add Custom Bootloader Hook Project

**Files:**
- Create: `bootloader/papers3_single_shot/CMakeLists.txt`
- Create: `bootloader/papers3_single_shot/main/CMakeLists.txt`
- Create: `bootloader/papers3_single_shot/main/placeholder.c`
- Create: `bootloader/papers3_single_shot/bootloader_components/papers3_boot_policy/CMakeLists.txt`
- Create: `bootloader/papers3_single_shot/bootloader_components/papers3_boot_policy/papers3_boot_policy.c`
- Create: `bootloader/papers3_single_shot/sdkconfig.defaults`

- [ ] **Step 1: Add minimal ESP-IDF project files**

Create `bootloader/papers3_single_shot/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(papers3_single_shot_bootloader)
```

Create `bootloader/papers3_single_shot/main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "placeholder.c")
```

Create `bootloader/papers3_single_shot/main/placeholder.c`:

```c
void app_main(void) {}
```

- [ ] **Step 2: Add bootloader build defaults**

Create `bootloader/papers3_single_shot/sdkconfig.defaults`:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
CONFIG_BOOTLOADER_OFFSET_IN_FLASH=0x0
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM_MODE_OCT=y
```

- [ ] **Step 3: Add bootloader component declaration**

Create `bootloader/papers3_single_shot/bootloader_components/papers3_boot_policy/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "papers3_boot_policy.c"
    INCLUDE_DIRS "../../../../include"
    REQUIRES bootloader_support
)
```

- [ ] **Step 4: Implement bootloader hook**

Create `papers3_boot_policy.c` that:

```c
#include <string.h>

#include "bootloader_common.h"
#include "bootloader_flash.h"
#include "esp_flash_partitions.h"
#include "esp32s3/rom/spi_flash.h"
#include "papers3_bootcfg.h"

static bool flash_read(uint32_t offset, void* out, uint32_t size) {
  return esp_rom_spiflash_read(offset, (uint32_t*)out, size) == ESP_ROM_SPIFLASH_RESULT_OK;
}

static bool flash_erase(uint32_t offset, uint32_t size) {
  bootloader_flash_unlock();
  return esp_rom_spiflash_erase_area(offset, size) == ESP_ROM_SPIFLASH_RESULT_OK;
}

static bool flash_write(uint32_t offset, const void* data, uint32_t size) {
  bootloader_flash_unlock();
  return esp_rom_spiflash_write(offset, (const uint32_t*)data, size) == ESP_ROM_SPIFLASH_RESULT_OK;
}

static void erase_otadata(void) {
  (void)flash_erase(PAPERS3_OTADATA_OFFSET, PAPERS3_OTADATA_SIZE);
}

static void consume_bootcfg(uint32_t generation) {
  papers3_bootcfg_record_t empty = papers3_bootcfg_make_empty(generation + 1u);
  if (flash_erase(PAPERS3_BOOTCFG_PARTITION_OFFSET, PAPERS3_BOOTCFG_PARTITION_SIZE)) {
    (void)flash_write(PAPERS3_BOOTCFG_PARTITION_OFFSET, &empty, sizeof(empty));
  }
}

static void write_runtime_otadata_once(void) {
  esp_ota_select_entry_t entry;
  memset(&entry, 0xFF, sizeof(entry));
  entry.ota_seq = 1u;
  entry.ota_state = ESP_OTA_IMG_UNDEFINED;
  entry.crc = bootloader_common_ota_select_crc(&entry);

  uint8_t sector[PAPERS3_OTADATA_SIZE];
  memset(sector, 0xFF, sizeof(sector));
  memcpy(sector, &entry, sizeof(entry));
  memcpy(sector + 0x1000, &entry, sizeof(entry));

  if (flash_erase(PAPERS3_OTADATA_OFFSET, PAPERS3_OTADATA_SIZE)) {
    (void)flash_write(PAPERS3_OTADATA_OFFSET, sector, sizeof(sector));
  }
}

void bootloader_after_init(void) {
  papers3_bootcfg_record_t record;
  if (flash_read(PAPERS3_BOOTCFG_PARTITION_OFFSET, &record, sizeof(record)) &&
      papers3_bootcfg_is_runtime_once(&record)) {
    consume_bootcfg(record.generation);
    write_runtime_otadata_once();
    return;
  }

  erase_otadata();
}
```

- [ ] **Step 5: Run source tests**

Run:

```bash
python3 -m unittest tests/test_papers3_single_shot_boot.py -v
```

Expected: bootloader hook source test passes.

## Task 6: Add PlatformIO Bootloader Integration

**Files:**
- Create: `tools/papers3_custom_bootloader.py`
- Modify: `platformio.ini`

- [ ] **Step 1: Add extra script**

Create `tools/papers3_custom_bootloader.py`:

```python
from pathlib import Path
from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()
project_dir = Path(env.subst("$PROJECT_DIR"))
custom_bootloader = (
    project_dir
    / "bootloader"
    / "papers3_single_shot"
    / "build"
    / "bootloader"
    / "bootloader.bin"
)

if custom_bootloader.exists():
    images = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        if str(offset).lower() == "0x0000":
            images.append((offset, str(custom_bootloader)))
        else:
            images.append((offset, image))
    env.Replace(FLASH_EXTRA_IMAGES=images)
else:
    print(
        "Warning: custom PaperS3 bootloader not found at "
        f"{custom_bootloader}; using PlatformIO default bootloader"
    )
```

- [ ] **Step 2: Wire the script into PaperS3 launcher/runtime envs**

For `papers3_buddy` and `papers3_launcher`, set:

```ini
extra_scripts =
	post:tools/papers3_custom_bootloader.py
```

Keep `papers3_buddy` existing source filters and dependencies unchanged.

- [ ] **Step 3: Run source tests**

Run:

```bash
python3 -m unittest tests/test_papers3_single_shot_boot.py tests/test_platform_layout.py -v
```

Expected: tests pass.

## Task 7: Build And Verify

**Files:**
- No source edits unless verification exposes build issues.

- [ ] **Step 1: Run full unit tests**

Run:

```bash
python3 -m unittest discover -s tests
```

Expected: all tests pass.

- [ ] **Step 2: Build launcher and runtime**

Run:

```bash
pio run -e papers3_launcher
pio run -e papers3_buddy
```

Expected: both builds succeed. If the custom bootloader binary has not been built yet, PlatformIO should warn and use the default bootloader for the app build artifact.

- [ ] **Step 3: Attempt custom bootloader build**

Run from `bootloader/papers3_single_shot`:

```bash
idf.py build
```

Expected: `build/bootloader/bootloader.bin` exists. If `idf.py` or ESP-IDF is unavailable, record that blocker and keep the app-side integration ready.

## Self-Review Notes

- Spec coverage: the plan adds `bootcfg`, launcher one-shot writes, custom bootloader behavior, default launcher fallback, and automated/source tests.
- Implementation refinement: the custom bootloader uses a hook to rewrite or erase `otadata` before stock app selection. This still satisfies the spec while avoiding a full app-selection fork.
- Placeholder scan: no task contains TBD or an undefined future step; the only external dependency risk is ESP-IDF availability for building the custom bootloader artifact.
