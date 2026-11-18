"""Build an audio diagnostic around the existing real Klatt preview waveform."""
from pathlib import Path
import array,hashlib,json,subprocess,sys,wave
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results/velxio';OUT.mkdir(exist_ok=True)
fixture=ROOT/'test-results/klatt-sample.wav'
with wave.open(str(fixture)) as wav:
 assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate())==(1,2,22050)
 samples=array.array('h',wav.readframes(wav.getnframes()))
 if sys.byteorder!='little':samples.byteswap()
(ROOT/'generated/audio_compare_fixture.h').write_text('#pragma once\nstatic constexpr int16_t kAudio[]={'+','.join(map(str,samples))+'};\nstatic constexpr size_t kAudioSamples=sizeof(kAudio)/sizeof(kAudio[0]);\n')
environment='velxio-audio-pipeline' if '--pipeline' in sys.argv else 'velxio-audio-compare'
result=subprocess.run([sys.executable,'-m','platformio','run','-e',environment,'-j','6'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(OUT/(environment+'-build.log')).write_bytes(result.stdout)
if result.returncode:raise SystemExit(result.returncode)
build=ROOT/'.pio/build'/environment;core=ROOT.parent/'Launcher/.pio/core';target=OUT/(environment+'-simulator.bin')
subprocess.run([str(core/'penv/Scripts/python.exe'),str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3','merge-bin',
 '--output',str(target),'--flash-mode','dio','--flash-freq','80m','--flash-size','8MB','0x0',str(build/'bootloader.bin'),
 '0x8000',str(build/'partitions.bin'),'0x10000',str(build/'firmware.bin')],check=True)
meta={'environment':environment,'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'samples':len(samples),'expected_audio_seconds':len(samples)/22050,
 'simulator_image_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'purpose':'Simulator comparison only; not a device installation image'}
(OUT/(environment+'-inputs.json')).write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta,indent=2))
