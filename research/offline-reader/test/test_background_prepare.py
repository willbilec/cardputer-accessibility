"""The unified build reserves its critical speech buffer deterministically."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('background_prepare', ROOT/'prepare_background.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

class BackgroundPrepareTest(unittest.TestCase):
    def test_reserved_phonemes_and_idempotence(self):
        directory = prepare.prepare()
        source = directory/'src/libespeak-ng/phonemelist.c'
        first = source.read_bytes()
        self.assertIn(b'static PHONEME_LIST reserved_ph_list3[N_PHONEME_LIST]', first)
        self.assertIn(b'#ifndef UNIFIED_RESERVED_PHONEMES', first)
        translation = directory/'src/libespeak-ng/translate.c'
        reserved = translation.read_bytes()
        for name in (b'reserved_charix', b'reserved_words', b'reserved_sbuf', b'reserved_num_wtab'):
            self.assertIn(name, reserved)
        waveform = directory/'src/libespeak-ng/synthesize.h'
        dry_speech = waveform.read_bytes()
        self.assertIn(b'#define N_ECHO_BUF 1\n#else', dry_speech.replace(b'\r\n', b'\n'))
        speech = (directory/'src/BackgroundAudioSpeech.h').read_text(encoding='utf-8')
        api = (directory/'src/libespeak-ng/speech.c').read_text(encoding='utf-8')
        phonemes = (directory/'src/libespeak-ng/phoneme.h').read_text(encoding='utf-8')
        frame_ms = 20
        frame_samples = (22_050 * frame_ms) // 1_000 + 1
        self.assertIn(f'framelen = {frame_samples}', speech)
        self.assertIn(f'min_buffer_length = {frame_ms}', api)
        self.assertIn('N_PHONEME_TABS     134', phonemes)
        prepare.prepare()
        self.assertEqual(source.read_bytes(), first)
        self.assertEqual(translation.read_bytes(), reserved)
        self.assertEqual(waveform.read_bytes(), dry_speech)

if __name__ == '__main__':
    unittest.main()
