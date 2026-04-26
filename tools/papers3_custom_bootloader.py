from pathlib import Path

from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()
board_config = env.BoardConfig()
project_dir = Path(env.subst("$PROJECT_DIR"))
custom_bootloader = (
    project_dir
    / "bootloader"
    / "papers3_single_shot"
    / "build"
    / "bootloader"
    / "bootloader.bin"
)
pio_custom_bootloader = (
    project_dir
    / "bootloader"
    / "papers3_single_shot"
    / ".pio"
    / "build"
    / "papers3_single_shot_bootloader"
    / "bootloader.bin"
)
selected_bootloader = None
for candidate in (custom_bootloader, pio_custom_bootloader):
    if candidate.exists():
        selected_bootloader = candidate
        break

if selected_bootloader is not None:
    board_config.update("build.arduino.custom_bootloader", str(selected_bootloader))
    images = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        if str(offset).lower() in ("0x0000", "0x0"):
            images.append((offset, str(selected_bootloader)))
        else:
            images.append((offset, image))
    env.Replace(FLASH_EXTRA_IMAGES=images)
else:
    print(
        "Warning: custom PaperS3 bootloader not found at "
        f"{custom_bootloader} or {pio_custom_bootloader}; "
        "using PlatformIO default bootloader"
    )
