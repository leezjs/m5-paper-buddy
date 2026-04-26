# PaperS3 Single-Shot Runtime Boot Design

## Goal

Make PaperS3 runtime boot one-shot:

- launcher remains the default boot destination
- launcher can request one runtime launch
- after that launch, any subsequent boot returns to launcher

This must work even for third-party runtime firmware that does not cooperate with the launcher.

## Why The Current Design Is Not Enough

The current phase-1 flow writes the runtime partition as the next boot target through OTA metadata and reboots. Once the device is in runtime, any later reset or power cycle continues to boot runtime unless that runtime explicitly clears `otadata`.

That behavior is acceptable for cooperative runtime firmware, but it fails the requirement for arbitrary imported images.

## Considered Approaches

### 1. Runtime clears `otadata`

This only works for firmware we control. It does not solve recovery from arbitrary third-party images.

### 2. Host-side recovery script only

This is still useful as an emergency path, but it keeps recovery dependent on a computer and does not provide device-side behavior.

### 3. Custom bootloader plus one-shot boot request

This allows launcher to request a single runtime boot without making runtime the long-term default target. It is the only approach that satisfies the requirement for unmodified third-party images.

This design chooses option 3.

## Partition Layout

Keep the current app offsets unchanged:

- `launcher` stays `factory` at `0x20000`
- `runtime` stays `ota_0` at `0x120000`

Add a small raw config partition in the currently unused gap between `phy_init` and `launcher`:

- `bootcfg` at `0x12000`
- size `0x1000`

This preserves the current launcher/runtime addresses and leaves the existing runtime import assumptions intact.

## Bootcfg Format

`bootcfg` is a raw fixed-size structure with:

- magic
- version
- command
- generation counter
- crc32

The only required command for the first slice is:

- `BOOT_RUNTIME_ONCE`

The structure is intentionally simple so both the launcher app and the bootloader can read and write it without NVS parsing.

## Boot Policy

Default rule:

- if no valid `bootcfg` command is present, boot `launcher`

Single-shot rule:

- if `bootcfg` contains a valid `BOOT_RUNTIME_ONCE` request, bootloader marks it consumed before jumping to `runtime`

Result:

- launcher can launch runtime once
- any later reboot, reset, watchdog, crash restart, or power cycle returns to launcher because the request has already been consumed

This is the intended behavior. Runtime is not a persistent boot destination anymore.

## Launcher Changes

When launcher chooses "Boot Current":

1. validate the runtime partition contains an app image
2. write `BOOT_RUNTIME_ONCE` into `bootcfg`
3. reboot

Launcher should no longer rely on `esp_ota_set_boot_partition(runtime)` for the normal PaperS3 launcher flow.

Launcher can still keep the existing "return to launcher" behavior for cooperative runtime firmware, but that path becomes optional rather than required for recovery.

## Bootloader Changes

Introduce a custom PaperS3 bootloader that:

1. reads the partition table
2. locates `bootcfg`, `launcher`, and `runtime`
3. validates `bootcfg`
4. if a valid one-shot runtime request exists, consumes it and boots `runtime`
5. otherwise boots `launcher`

The bootloader should not depend on runtime cooperation or on mutable OTA state for the normal boot decision.

Implementation refinement: the first implementation uses an ESP-IDF bootloader hook rather than replacing the whole bootloader selection path. The hook consumes a valid `bootcfg` request by writing a one-time `runtime` OTA selection into `otadata`; when no valid request exists, it erases `otadata` before stock app selection runs, so the stock bootloader falls back to the factory `launcher`.

## Build Integration

The current Arduino PlatformIO flow can replace the flashed bootloader image, but it expects a ready-made `bootloader.bin`.

Implementation should therefore add:

- a small dedicated bootloader source tree under the repo
- a build step that produces `bootloader.bin` for PaperS3
- an extra script that swaps the default bootloader entry in `FLASH_EXTRA_IMAGES` with the generated custom bootloader

This keeps the main app environments on Arduino while allowing the bootloader itself to be built with the required ESP-IDF bootloader sources.

## Error Handling

If `bootcfg` is missing, corrupted, unknown, or CRC-invalid:

- boot launcher

If `runtime` is requested but its partition does not contain a valid image:

- clear the request
- boot launcher

If launcher fails to write a valid `bootcfg` request:

- show an error and do not reboot

Failure mode bias should always favor launcher recovery.

## Testing Strategy

### Automated

- partition layout test for the new `bootcfg` entry
- source-level tests for launcher writing one-shot boot requests
- bootcfg encode/decode unit tests

### Device

- boot launcher from a clean flash
- request one runtime boot and confirm runtime starts
- press the physical side key to reset and confirm launcher returns
- trigger `esp_restart()` from a cooperative runtime and confirm launcher returns
- power cycle after one runtime boot and confirm launcher returns
- verify third-party runtime still boots for the first single-shot launch

## Risks

### Build Complexity

Custom bootloader integration is the main implementation risk. The app logic is straightforward; the build chain wiring is not.

### Third-Party Runtime Assumptions

Some imported firmware may expect persistent OTA metadata. This design intentionally removes that assumption for the PaperS3 launcher flow.

### Recovery Safety

Bootcfg corruption must never strand the device in runtime. Invalid config always falling back to launcher is a hard requirement.

## Decision

Proceed with a custom PaperS3 bootloader and a raw `bootcfg` partition that implements one-shot runtime boot. Launcher remains the default destination for every boot except the immediately requested runtime launch.
