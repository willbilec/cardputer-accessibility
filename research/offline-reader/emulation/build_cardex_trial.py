"""Build the modified community app and merge a simulator-only factory image."""
from pathlib import Path
import hashlib,json,runpy,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT.parent/'CardEX';OUT=ROOT/'test-results/cardex';OUT.mkdir(exist_ok=True)
runpy.run_path(str(ROOT/'prepare_background.py'))['prepare']()
result=subprocess.run([sys.executable,'-m','platformio','run','-c','platformio-access.ini','-e','cardex-access','-j','6'],cwd=APP,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(OUT/'build.log').write_bytes(result.stdout)
if result.returncode:raise SystemExit(result.returncode)
build=APP/'.pio-access/build/cardex-access';core=ROOT.parent/'Launcher/.pio/core'
for name in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin'):shutil.copy2(build/name,OUT/name)
target=OUT/'cardex-access-simulator.bin'
subprocess.run([str(core/'penv/Scripts/python.exe'),str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3','merge-bin','--output',str(target),
 '--flash-mode','dio','--flash-freq','80m','--flash-size','8MB','0x0',str(OUT/'bootloader.bin'),'0x8000',str(OUT/'partitions.bin'),'0x10000',str(OUT/'firmware.bin')],check=True)
sources=sorted((APP/'src').rglob('*'))+[APP/'platformio-access.ini',APP/'partitions-access.csv']
meta={'upstream_base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=APP,text=True).strip(),
 'image_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'app_bytes':(OUT/'firmware.bin').stat().st_size,
 'sources':{str(p.relative_to(APP)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources if p.is_file()},
 'purpose':'Simulator-only accessible CardEX experiment; no physical installation or SD catalogue update'}
assert meta['app_bytes']<=0x7e0000
(OUT/'inputs.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in meta.items() if k!='sources'},indent=2))
