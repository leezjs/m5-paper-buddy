import json
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
IMPORT = REPO_ROOT / "tools" / "firmware_import.py"
FULL_FLASH = REPO_ROOT / "firmware" / "buddy.bin"
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
            manifest = json.loads(
                (package_dir / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["compatibility"], "full-flash-only")
            self.assertEqual(manifest["install_mode"], "host-flash")

    def test_runtime_candidate_image_is_packaged_as_switchable(self):
        with tempfile.TemporaryDirectory() as tmp:
            package_dir = run_import(RUNTIME_SAMPLE, "sample-runtime", Path(tmp))
            manifest = json.loads(
                (package_dir / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["compatibility"], "switchable")
            self.assertEqual(manifest["install_mode"], "runtime-slot")
            self.assertTrue((package_dir / "app.bin").exists())


if __name__ == "__main__":
    unittest.main()
