"""Exercise the real upstream integration and fail closed on changed anchors."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_reader", ROOT / "prepare.py")
prepare = importlib.util.module_from_spec(spec)
# The build hook uses SCons Import when imported. Load only its reusable definitions.
exec(compile((ROOT / "prepare.py").read_text(encoding="utf-8").split('if __name__ == "__main__":')[0], str(ROOT / "prepare.py"), "exec"), prepare.__dict__)

class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.code = prepare.generate_player()

    def test_upstream_fail_closed(self):
        with self.assertRaises(ValueError): prepare.replace_once("two two", "two", "new")
        with self.assertRaises(ValueError): prepare.replace_once("missing", "two", "new")

    def test_passwords_and_data(self):
        self.assertIn('textInputTarget == 0 || textInputTarget == 2', self.code)
        self.assertNotIn('nvs_flash_erase()', self.code)
        self.assertIn('"reader_music"', self.code)
        self.assertIn('"/reader-playlist.txt"', self.code)
        self.assertIn('"/reader-config.txt"', self.code)

    def test_single_input_update(self):
        loop = self.code.split('static void playerLoop() {')[1].split('\nvoid loop() {')[0]
        self.assertNotIn('M5Cardputer.update()', loop)
        self.assertIn('readerHandleKeys()', self.code.split('\nvoid loop() {')[1])

    def test_shared_settings_and_empty_library(self):
        self.assertIn('M5Cardputer.Display.print(readerSettingsLabel(idx))', self.code)
        self.assertIn('ReaderText capture;', self.code)
        self.assertEqual(self.code.count("&& !audioApp.songOffsets.empty()) { audioApp.browserIndex"), 2)
        self.assertIn('readerPlaybackReady = true', self.code)
        self.assertNotIn('SplashScreen::show();', self.code)

    def test_headless_removes_graphics(self):
        code = prepare.generate_player(headless=True)
        for dependency in ('M5Unified', 'M5GFX', 'LGFX_Sprite', 'fonts::', 'lgfx::', 'class BootMenu', 'class SplashScreen', 'class fft_t', 'static fft_t fft;'):
            self.assertNotIn(dependency, code)
        self.assertIn('headlessNextSetting', code)
        self.assertIn('int UIManager::settingsCursor = 4;', code)
        self.assertIn('"headless_music"', code)
        self.assertIn('readerHandleKeys()', code)
        self.assertIn('static void drawTextInput() {}', code)

if __name__ == "__main__": unittest.main()
