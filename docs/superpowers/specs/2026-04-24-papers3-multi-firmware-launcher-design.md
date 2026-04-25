# PaperS3 Multi-Firmware Launcher Design

Date: 2026-04-24
Status: Approved for planning
Owner: Codex + repository maintainer

## Summary

This repository should evolve from a single-purpose `paper-buddy` firmware into a small firmware platform for `M5PaperS3 / ESP32-S3 / 16MB flash`:

- a permanent `launcher` that always remains installable and recoverable
- one large `runtime` slot that holds the currently selected independent firmware
- desktop-side import tools that inspect exported `M5Burner` binaries and convert compatible ones into installable packages
- `paper-buddy` itself treated as one installable package rather than the only built-in firmware

The primary goal is not "keep many arbitrary Burner firmwares resident at once." The primary goal is "switch between independent firmwares from a common launcher workflow, while tolerating the inconsistent shapes of real Burner exports."

This direction is driven by inspection of real sample files already present in the repo:

- [firmware/esp32fw.bin](/Users/bytedance/workspace/self/m5-paper-buddy/firmware/esp32fw.bin): full `16MB` flash image with `factory app + large spiffs`
- [firmware/d07ae7625d1e15309886e9e884767ff7.bin](/Users/bytedance/workspace/self/m5-paper-buddy/firmware/d07ae7625d1e15309886e9e884767ff7.bin): partial export with bootloader + partition table + a very large `15MB` app image

These two examples prove that `M5Burner` exports do not share one predictable resident layout. Therefore a resident multi-slot architecture for arbitrary third-party firmware is not the right baseline.

## Current Repository Context

The repository today is organized as a single firmware product plus a host bridge:

- [src/paper/main.cpp](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/main.cpp): main UI, touch handling, prompt rendering, settings, and most device behavior in one file
- [src/paper/data_paper.h](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/data_paper.h): heartbeat/protocol parsing for the Claude bridge
- [src/paper/xfer_paper.h](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/xfer_paper.h): command transport helpers
- [src/paper/paper_compat.h](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/paper_compat.h): board abstraction for PaperS3 vs older M5Paper
- [tools/claude_code_bridge.py](/Users/bytedance/workspace/self/m5-paper-buddy/tools/claude_code_bridge.py): desktop daemon for Claude Code integration
- [platformio.ini](/Users/bytedance/workspace/self/m5-paper-buddy/platformio.ini): single-firmware build environments
- [partitions-m5paper.csv](/Users/bytedance/workspace/self/m5-paper-buddy/partitions-m5paper.csv): one app partition plus a large filesystem
- [plugin/scripts/flash.sh](/Users/bytedance/workspace/self/m5-paper-buddy/plugin/scripts/flash.sh): assumes "flash firmware + filesystem" as one product flow

The current codebase is optimized for a single continuously-developed firmware. It is not yet structured for:

- a small always-available launcher
- firmware packaging and installation
- import/analysis of external firmware binaries
- a neutral runtime handoff mechanism independent of Claude Code

## Goals

1. Allow a user to switch between fully independent firmware experiences on one PaperS3 device.
2. Keep `paper-buddy` as one selectable firmware rather than hard-coding it as the system identity.
3. Support imported firmware that originated from `M5Burner`, as long as the import tool can classify and package it safely.
4. Preserve a reliable recovery path even when the selected runtime firmware cannot cooperate with the launcher.
5. Make the architecture explicit enough that implementation can proceed in staged milestones.

## Non-Goals

1. Guarantee that every `M5Burner` firmware can be kept resident alongside every other firmware.
2. Guarantee in-place return from arbitrary third-party runtime firmware back to the launcher without reboot or recovery controls.
3. Preserve every original source partition layout from imported firmware after installation.
4. Solve cryptographic secure boot, signed package verification, or anti-rollback in the first iteration.

## Observations From Real Firmware Samples

### Sample A: `esp32fw.bin`

Observed properties:

- file size is exactly `16,777,216` bytes
- contains valid ESP32-S3 bootloader at offset `0x0000`
- contains a valid partition table at offset `0x8000`
- partition table decodes to:

```csv
nvs,data,nvs,0x9000,20K
otadata,data,ota,0xe000,8K
app0,app,factory,0x10000,3M
spiffs,data,spiffs,0x310000,13184K
```

Interpretation:

- this looks like a full-flash export or a merged full image
- it is useful as an import source
- it is not a good candidate for "resident side-by-side coexistence" with other unrelated firmware because it assumes most of flash for its own app + data model

### Sample B: `d07ae7625d1e15309886e9e884767ff7.bin`

Observed properties:

- file size is `13,949,216` bytes, not the full `16MB`
- still includes valid bootloader + partition table at the standard offsets
- partition table decodes to:

```csv
nvs,data,nvs,0x9000,20K
otadata,data,ota,0xe000,8K
app0,app,ota_0,0x10000,15M
spiffs,data,spiffs,0xf10000,896K
coredump,data,coredump,0xff0000,64K
```

- the payload after `0x10000` can be parsed as a valid ESP32-S3 application image

Interpretation:

- some Burner exports are effectively "bootloader + partition table + one huge app payload"
- these images are closer to installable single-runtime payloads
- the fact that this sample uses almost all flash for the application confirms that a generic multi-resident scheme will fail quickly

## Architectural Options Considered

### Option 1: Permanent multi-slot resident firmwares

Description:

- reserve several application partitions permanently
- keep `paper-buddy`, reader firmware, and additional apps all resident at once
- switch by updating boot target and rebooting

Advantages:

- fastest runtime switching
- no reinstall step once images are present

Disadvantages:

- only works when all firmwares share one carefully designed partition table
- incompatible with the real Burner exports already observed
- forces every app to fit a reduced slot budget, which breaks the `15MB app` example immediately

Decision:

- reject as the primary product architecture

### Option 2: Launcher + one normalized runtime slot

Description:

- keep one small permanent launcher
- reserve one very large runtime app slot
- desktop-side import tools inspect exported firmware and convert compatible ones into a normalized package format
- launcher installs the selected package into the runtime slot and reboots into it

Advantages:

- matches the diversity of real Burner output
- keeps a simple device mental model
- makes `paper-buddy` and third-party firmware first-class peers
- enables compatibility classification instead of pretending all firmware is switchable in the same way

Disadvantages:

- switching may require an install/write step, not only a reboot
- some imported images may still be downgraded to "external flash only" or "unsupported"
- recovery flow needs explicit design

Decision:

- choose as the primary architecture

### Option 3: Host-only full reflash manager

Description:

- keep the current repo mostly as-is
- add only desktop tooling that reflashes complete images over USB whenever the user wants to switch

Advantages:

- highest compatibility with arbitrary binaries
- least device-side complexity

Disadvantages:

- fails the desired product experience of switching from a device-side interface
- cannot present itself as a launcher-based platform
- still leaves the firmware codebase monolithic

Decision:

- keep as a fallback/recovery capability, not as the main architecture

## Recommended Architecture

The repository should adopt a three-layer model:

1. `launcher`
   A small, always-installable PaperS3 firmware responsible for listing packages, showing compatibility notes, installing a selected package into the runtime slot, and entering recovery/install mode.

2. `runtime package`
   A normalized package format derived either from repository-owned firmware builds or from inspected Burner exports. `paper-buddy` becomes one such package.

3. `desktop tooling`
   Inspection and conversion scripts that turn raw firmware exports into structured package directories with a manifest, payload files, and compatibility metadata.

The most important architectural boundary is this:

- launcher code must not depend on Claude Code protocol concepts
- `paper-buddy` may continue to depend on the bridge daemon
- import tooling must classify firmware honestly rather than forcing every binary into one false abstraction

## Repo Restructure

### Target Source Layout

```text
src/
  apps/
    launcher/
      main.cpp
      ui/
      install/
      package_index/
    buddy/
      main.cpp
      protocol/
      dashboard/
      prompts/
  common/
    paper_hw/
    display/
    touch/
    storage/
    bootcfg/
    package/
    install/
  legacy/
    m5paper/

tools/
  claude_code_bridge.py
  firmware_inspect.py
  firmware_import.py
  firmware_package.py
  firmware_flash.py

firmware/
  raw/
  packages/
    paper-buddy/
    <imported-id>/
```

### Migration of Existing Code

- extract hardware abstraction from [src/paper/paper_compat.h](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/paper_compat.h) into `src/common/paper_hw/`
- keep BLE and low-level device support reusable rather than `paper`-specific
- split [src/paper/main.cpp](/Users/bytedance/workspace/self/m5-paper-buddy/src/paper/main.cpp) into:
  - shared display/touch/util layers
  - `buddy`-specific protocol screens
  - launcher-specific screens
- keep [tools/claude_code_bridge.py](/Users/bytedance/workspace/self/m5-paper-buddy/tools/claude_code_bridge.py) dedicated to the `paper-buddy` runtime package
- convert the current plugin flash flow so it can flash the launcher separately from installing packages

## Flash Layout Strategy

### Principle

Use one fixed launcher-owned partition layout on the device. Imported firmware does not control the resident partition table after import. Instead, the import pipeline decides whether a raw export can be mapped into the normalized runtime model.

### Baseline Layout

The phase-1 partition table should be fixed to this exact shape:

```csv
nvs,data,nvs,0x9000,24K
otadata,data,ota,0xf000,8K
phy_init,data,phy,0x11000,4K
launcher,app,factory,0x20000,1M
runtime,app,ota_0,0x120000,0xDF0000
storage,data,spiffs,0xF10000,0x0E0000
coredump,data,coredump,0xFF0000,0x010000
```

Rationale:

- `launcher` gets enough room for a touch UI, package metadata, install logic, and recovery affordances
- `runtime` gets `0xDF0000` bytes, which is large enough for the inspected `15MB class` imported application sample
- `storage` remains available for manifests, temporary package state, and launcher resources
- the layout is owned by this repo, not by imported packages
- aligning launcher and runtime to clean app offsets keeps future boot handling straightforward

### Why not multiple runtime slots

Because the observed imported apps can consume nearly all remaining flash. Multiple resident runtime slots would either:

- make large imported images impossible, or
- produce a complex matrix of slot classes that is not worth the product cost in the first generation

## Package Model

Each installable firmware package should live under `firmware/packages/<id>/`:

```text
firmware/packages/<id>/
  manifest.json
  app.bin
  data.bin
  icon.bmp
  notes.txt
  original/
    exported.bin
    partition_table.csv
```

### Manifest Fields

The manifest should minimally contain:

```json
{
  "id": "paper-buddy",
  "name": "Paper Buddy",
  "version": "0.1.0",
  "source_type": "repo-build",
  "chip": "esp32s3",
  "flash_size": 16777216,
  "install_mode": "runtime-slot",
  "compatibility": "switchable",
  "runtime": {
    "app_offset": 0,
    "app_file": "app.bin",
    "data_file": "data.bin"
  },
  "recovery": {
    "returns_to_launcher": false
  },
  "notes": [
    "Reboot required after install."
  ]
}
```

### Compatibility Classes

Import tooling should assign one of these classes:

- `switchable`
  Can be installed into the normalized runtime slot and launched from the launcher.
- `installable-only`
  Can be written by the launcher or host tools, but return-to-launcher behavior is weak or unknown.
- `needs-repack`
  Payload is usable, but filesystem/data extraction or address remapping is still required.
- `full-flash-only`
  Must be flashed as a complete image over USB from the host.
- `unsupported`
  Not safe to install on this device/layout.

This classification should be visible both in tooling output and in launcher UI.

## Import Pipeline

### `firmware_inspect.py`

Responsibilities:

- identify chip family and flash assumptions
- detect whether the file is:
  - full flash image
  - merged image with bootloader + partition table + app
  - application image only
- decode partition table when present
- extract app metadata using `esptool.py image_info`
- emit a machine-readable JSON report

### `firmware_import.py`

Responsibilities:

- take raw firmware input and an inspect report
- classify compatibility
- extract installable payloads when possible
- generate `manifest.json`
- store original artifacts for traceability

### Initial Heuristics

- if a valid app image can be isolated and it fits inside the runtime partition, prefer packaging it as `runtime-slot`
- if a raw export requires its own partition topology or oversized filesystem image, classify as `full-flash-only` or `needs-repack`
- never silently truncate or remap without recording it in the manifest

## Launcher Responsibilities

The launcher should remain intentionally narrow.

### Required Features

1. Show installed/available packages.
2. Show package metadata and compatibility notes.
3. Install the selected package to the runtime slot.
4. Boot the runtime firmware.
5. Expose recovery/install mode.
6. Offer a direct way to reinstall `paper-buddy`.

### Explicit Non-Responsibilities

1. Speaking the Claude bridge protocol.
2. Understanding the business logic of imported firmware.
3. Pretending to live-switch independent firmware without reboot.

### UI Notes

The launcher UI can reuse existing PaperS3 rendering primitives but should be visually simpler than `paper-buddy`:

- package list
- detail pane
- status/progress panel
- recovery/help panel

No part of launcher flow should depend on the host daemon being alive.

## Boot and Recovery Strategy

### Phase 1

- launcher is flashed as the factory app
- installing a package writes the runtime slot
- launcher sets the next boot target to runtime and reboots
- recovery back to launcher is done through a hardware button combination on boot or a host-side reflash command

### Phase 2

- add a stronger boot override path so holding a specific button during reset always enters launcher
- optionally add a lightweight boot-state flag in NVS so launcher can reclaim control after repeated runtime boot failures

This staged approach avoids prematurely implementing a custom bootloader before the package model is proven.

## Build and Flash Workflow Changes

### PlatformIO

[platformio.ini](/Users/bytedance/workspace/self/m5-paper-buddy/platformio.ini) should grow separate environments:

- `papers3_launcher`
- `papers3_buddy`
- optional legacy envs kept temporarily for migration

`paper-buddy` should build as a runtime package, not as the default device identity.

### Plugin / Scripts

[plugin/scripts/flash.sh](/Users/bytedance/workspace/self/m5-paper-buddy/plugin/scripts/flash.sh) should be replaced or extended with flows like:

- flash launcher base image
- install package by id
- recover to launcher
- inspect/import raw firmware

The existing "upload filesystem then upload firmware" assumption only remains valid for the `paper-buddy` package build process, not for the whole platform.

## Data and Filesystem Strategy

First-generation launcher storage should be minimal:

- package index metadata
- install progress / temp state
- optional thumbnails/icons
- recovery state flags

Imported firmware data partitions should not be assumed installable unless the import tool can map them safely. For many external images, application payload may be installable while bundled filesystem content remains unsupported or requires repack.

For phase 1, package installation is host-assisted rather than SD-first:

- raw exports are inspected and converted on the desktop
- normalized package files are copied to the device by a host tool
- direct SD-card browsing/import from the launcher is explicitly deferred until after the package format is stable

For phase 1, `paper-buddy` may keep an optional package data image:

- `app.bin` is always required
- `data.bin` is optional and installed only when present
- this avoids forcing an immediate rewrite of all current asset handling before the launcher flow exists

## Decisions Fixed For Planning

The following decisions are now fixed and should not be reopened during implementation planning unless device testing disproves them:

1. The launcher partition is `1MB` at `0x20000`.
2. The runtime slot is `0xDF0000` bytes at `0x120000`.
3. First-generation package import is host-assisted, not launcher-side SD import.
4. First-generation recovery does not require a custom bootloader; it relies on factory launcher placement, boot-target switching, and a hardware-button recovery path.
5. `paper-buddy` will be packaged as a runtime app with an optional package data image instead of requiring a full embedded-assets rewrite in the first milestone.

## Risks

### Compatibility Risk

Imported Burner outputs may vary more widely than the two current samples. The tooling must report compatibility conservatively.

### Recovery Risk

Some runtime firmware may not cooperate with returning to launcher. Hardware-override recovery must exist before claiming a smooth switching experience.

### Monolith Risk

If `launcher` reuses `paper-buddy` code by copy-paste instead of extracting shared layers, the repo will become harder to evolve than it is today.

### User Expectation Risk

Users may assume "multiple firmware" means all firmware stays installed simultaneously. Documentation and UI must explain the difference between:

- installed package catalog
- currently active runtime
- host-only raw full-flash images

## Testing Strategy

### Tooling Tests

- sample-based inspection tests using committed fixture binaries
- manifest generation snapshot tests
- classification tests for edge cases

### Device Tests

- flash launcher on a clean PaperS3
- install `paper-buddy` package and confirm normal operation
- install a large imported app package and confirm boot success
- verify recovery to launcher via hardware path

### Regression Tests

- `paper-buddy` bridge behavior must stay unchanged once installed as runtime
- legacy single-firmware flash path should remain available during migration

## Implementation Milestones

### Milestone 1: Repo groundwork

- split shared hardware/display code from `paper-buddy`
- add launcher and buddy app targets
- establish the new partition layout

### Milestone 2: Packaging toolchain

- implement `firmware_inspect.py`
- implement `firmware_import.py`
- package `paper-buddy` as the first normalized package

### Milestone 3: Launcher MVP

- list packages
- show metadata
- install selected package into runtime slot
- boot runtime

### Milestone 4: Recovery and UX hardening

- button-based recovery path
- better failure messages
- optional host-side recovery helpers

## Final Recommendation

Proceed with a launcher-centered architecture built around one normalized runtime slot and conservative host-side firmware import tooling.

Treat imported `M5Burner` firmware as artifacts to inspect and classify, not as resident apps to promise can all coexist in flash. This recommendation matches the real firmware evidence already collected, preserves a practical recovery path, and gives the repository a clear path from "one firmware project" to "small firmware platform."
