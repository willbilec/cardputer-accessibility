"""Validate saved input identity and observation consistency, not audio quality."""
from pathlib import Path
import hashlib,json,math,wave,zipfile
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'test-results/velxio'
inputs=json.loads((out/'inputs.json').read_text());observed=json.loads((out/'observations.json').read_text())
assert observed['sdk_control']['enable_result']==0 and not observed['key_learning']['usable_audio']
assert not observed['live_queued_variant']['promoted_to_default']
with zipfile.ZipFile(ROOT/'headless-klatt-test-package.zip') as archive:
 app=archive.read('sd-card/accessible/key-learning.bin')
 key=inputs['key-learning'];wrapper=(ROOT/key['file']).read_bytes()
 assert wrapper[0x10000:0x10000+len(app)]==app
 assert hashlib.sha256(app).hexdigest()==key['app_sha256']
 assert hashlib.sha256(wrapper).hexdigest()==key['simulator_image_sha256']
with wave.open(str(ROOT/'test-results/klatt-sample.wav')) as fixture:
 assert fixture.getnframes()==186780 and fixture.getframerate()==22050
for name in ('prerecorded_large_chunks','prerecorded_small_chunks'):
 p=observed[name];assert p['blocks']==math.ceil(p['samples']/p['chunk_samples'])
 assert abs(p['emulated_ms']-p['samples']*1000/p['sample_rate'])<100
for metadata,binary in [('audio-compare-inputs.json','audio-compare-simulator.bin'),
 ('velxio-audio-pipeline-inputs.json','velxio-audio-pipeline-simulator.bin')]:
 data=json.loads((out/metadata).read_text())
 assert hashlib.sha256((out/binary).read_bytes()).hexdigest()==data['simulator_image_sha256']
print('Input hashes, packaged learner identity and transcribed comparison-record consistency PASS; live-audio usability remains FAILED by user listening report')
