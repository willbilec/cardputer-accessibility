"""Refresh documentation only after verifying all existing staged binaries."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];dist=ROOT/'dist-headless'
manifest=json.loads((dist/'manifest.json').read_text())
for name,record in manifest['files'].items():
 raw=(dist/name).read_bytes()
 assert len(raw)==record['bytes'] and hashlib.sha256(raw).hexdigest()==record['sha256'],name
for name in ('README.md','RESULTS.md','HEADLESS_RESULTS.md','HID_AND_LEARNING.md','ADVANCED_TESTS.md','AUDIO_RESEARCH.md','VELXIO_RESULTS.md'):
 shutil.copy2(ROOT/name,dist/name)
archive=shutil.make_archive(str(ROOT/'headless-klatt-test-package'),'zip',dist)
print('Updated package reports; all six binary artifacts retain their recorded hashes:',archive)
