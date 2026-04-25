import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
LAUNCHER_SOURCE = REPO_ROOT / "src" / "apps" / "launcher" / "main.cpp"


class LauncherSourceTests(unittest.TestCase):
    def test_launcher_uses_touch_ui_instead_of_button_prompts(self):
        text = LAUNCHER_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("Press B to boot", text)
        self.assertNotIn("A: refresh", text)
        self.assertNotIn("B: boot runtime", text)
        self.assertNotIn("C: restart launcher", text)
        self.assertIn("Touch.getDetail", text)
        self.assertIn("#include <SD.h>", text)
        self.assertIn('"/sd/packages"', text)
        self.assertIn("manifest.json", text)
        self.assertIn("installSelectedPackage", text)
        self.assertIn("setRotation(0)", text)
        self.assertIn("drawInstallScreen", text)
        self.assertIn("epd_fast", text)


if __name__ == "__main__":
    unittest.main()
