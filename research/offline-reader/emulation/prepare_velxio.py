"""Wrap existing app binaries for simulator import; never flash a device."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
CORE=ROOT.parent/'Launcher/.pio/core'
OUT=ROOT/'test-results/velxio'
OUT.mkdir(exist_ok=True)
manifest=json.loads((ROOT/'dist-headless/manifest.json').read_text())
results={}
for env,name in [('key-learning','key-learning'),('headless-klatt-launcher','catalogue-launcher')]:
 build=ROOT/'.pio/build'/env
 image=build/'firmware.bin';raw=image.read_bytes()
 assert 0<len(raw)<=0x280000
 packaged=ROOT/'dist-headless'/('sd-card/accessible/key-learning.bin' if name=='key-learning' else 'offline-test-launcher.bin')
 assert raw==packaged.read_bytes()
 target=OUT/(name+'-simulator.bin')
 subprocess.run([str(CORE/'penv/Scripts/python.exe'),str(CORE/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3',
  'merge-bin','--output',str(target),'--flash-mode','dio','--flash-freq','80m','--flash-size','8MB',
  '0x0',str(build/'bootloader.bin'),'0x8000',str(build/'partitions.bin'),'0x10000',str(image)],check=True)
 results[name]={'app_sha256':hashlib.sha256(raw).hexdigest(),'simulator_image_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
  'app_bytes':len(raw),'file':str(target.relative_to(ROOT)),
  'purpose':'Simulator-only factory-slot wrapper; application bytes match current SD package; not a device installation image'}
(OUT/'inputs.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
print(json.dumps(results,indent=2))
