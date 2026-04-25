import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class PlatformLayoutTests(unittest.TestCase):
    def test_platformio_declares_launcher_and_buddy_envs(self):
        text = (REPO_ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("[env:papers3_buddy]", text)
        self.assertIn("[env:papers3_launcher]", text)
        self.assertIn("board_build.app_partition_name = runtime", text)
        self.assertIn("board_build.app_partition_name = launcher", text)
        self.assertGreaterEqual(
            text.count("bblanchon/ArduinoJson @ ^7.0.0"),
            2,
        )

    def test_launcher_partition_csv_matches_phase1_layout(self):
        csv_path = REPO_ROOT / "partitions-papers3-launcher.csv"
        self.assertTrue(csv_path.exists(), f"missing {csv_path}")
        text = csv_path.read_text(encoding="utf-8")
        self.assertIn("nvs,data,nvs,0x9000,20K,", text)
        self.assertIn("otadata,data,ota,0xE000,8K,", text)
        self.assertIn("launcher,app,factory,0x20000,1M,", text)
        self.assertIn("runtime,app,ota_0,0x120000,0xDF0000,", text)
        self.assertIn("storage,data,spiffs,0xF10000,0x0E0000,", text)
        self.assertIn("coredump,data,coredump,0xFF0000,0x010000,", text)


if __name__ == "__main__":
    unittest.main()
