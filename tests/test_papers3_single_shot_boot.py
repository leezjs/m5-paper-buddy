import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BOOTCFG_HEADER = REPO_ROOT / "include" / "papers3_bootcfg.h"
BOOTCFG_IMPL = REPO_ROOT / "src" / "apps" / "launcher" / "bootcfg.cpp"
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
BOOTLOADER_PLATFORMIO = (
    REPO_ROOT / "bootloader" / "papers3_single_shot" / "platformio.ini"
)
BOOTLOADER_CMAKELISTS = (
    REPO_ROOT / "bootloader" / "papers3_single_shot" / "CMakeLists.txt"
)


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
        self.assertIn("runtimeOnceBootRequested()", text)
        self.assertIn("clearRuntimeBootRequest()", text)
        self.assertNotIn("esp_ota_set_boot_partition(runtime)", text)

    def test_custom_bootloader_hook_consumes_request_and_defaults_launcher(self):
        text = BOOTLOADER_HOOK.read_text(encoding="utf-8")
        self.assertIn("bootloader_hooks_include", text)
        self.assertIn("bootloader_after_init", text)
        self.assertIn("papers3_bootcfg_is_runtime_once", text)
        self.assertIn("write_runtime_otadata_once", text)
        self.assertIn("erase_otadata", text)
        self.assertIn("consume_bootcfg", text)

    def test_platformio_uses_custom_bootloader_script_for_papers3_launcher_layout(self):
        text = PLATFORMIO.read_text(encoding="utf-8")
        self.assertIn("tools/papers3_custom_bootloader.py", text)
        self.assertIn("PAPERS3_SINGLE_SHOT_BOOT=1", text)

    def test_launcher_bootcfg_helpers_cover_read_write_and_clear(self):
        text = BOOTCFG_IMPL.read_text(encoding="utf-8")
        self.assertIn("runtimeOnceBootRequested()", text)
        self.assertIn("clearRuntimeBootRequest()", text)
        self.assertIn("papers3_bootcfg_is_runtime_once", text)
        self.assertIn("papers3_bootcfg_make_empty", text)

    def test_bootloader_platformio_uses_espidf_main_as_source_dir(self):
        text = BOOTLOADER_PLATFORMIO.read_text(encoding="utf-8")
        self.assertIn("[platformio]", text)
        self.assertIn("src_dir = main", text)

    def test_bootloader_project_disables_unused_component_manager(self):
        text = BOOTLOADER_CMAKELISTS.read_text(encoding="utf-8")
        self.assertIn('set(ENV{IDF_COMPONENT_MANAGER} "0")', text)


if __name__ == "__main__":
    unittest.main()
