import json
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
INSPECT = REPO_ROOT / "tools" / "firmware_inspect.py"
FULL_FLASH = REPO_ROOT / "firmware" / "buddy.bin"
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
