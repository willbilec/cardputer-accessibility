"""Build accessible Cardulator and a verified simulator-only merged image."""
from pathlib import Path
import hashlib,json,runpy,shutil,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];APP=ROOT.parent/'cardulator'
OUT=ROOT/'test-results/cardulator';OUT.mkdir(exist_ok=True)
runpy.run_path(str(APP/'prepare_access.py'),run_name='__main__')
result=subprocess.run([sys.executable,'-m','platformio','run','-c','platformio-access.ini','-e','cardulator-access','-j','6'],cwd=APP,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(OUT/'build.log').write_bytes(result.stdout)
if result.returncode:raise SystemExit(result.returncode)
build=APP/'.pio-access/build/cardulator-access';core=ROOT.parent/'Launcher/.pio/core'
for source,target_name in [('cardulator.bin','firmware.bin'),('cardulator.elf','firmware.elf'),('bootloader.bin','bootloader.bin'),('partitions.bin','partitions.bin')]:shutil.copy2(build/source,OUT/target_name)
table=(OUT/'partitions.bin').read_bytes();factory=None
for i in range(0,len(table)-31,32):
 magic,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',table[i:i+32])
 if magic==0x50aa and kind==0 and subtype==0:factory=(offset,size)
assert factory and factory[0]==0x10000
assert 0<(OUT/'firmware.bin').stat().st_size<=factory[1]
target=OUT/'cardulator-access-simulator.bin'
subprocess.run([str(core/'penv/Scripts/python.exe'),str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3','merge-bin','--output',str(target),'--flash-mode','dio','--flash-freq','80m','--flash-size','8MB',
 '0x0',str(OUT/'bootloader.bin'),'0x8000',str(OUT/'partitions.bin'),'0x10000',str(OUT/'firmware.bin')],check=True)
image=target.read_bytes();assert image[0x8000:0x8000+len(table)]==table
sources=sorted((APP/'src').rglob('*'))+[APP/'platformio-access.ini',APP/'partitions-access.csv',APP/'prepare_access.py',APP/'build_tinyexpr.py',APP/'accessibility_units.dat']
meta={'upstream_base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=APP,text=True).strip(),
 'speech_upstream':'94641100866beae2cc97dc925c0d4562ad168879',
 'math_submodules':subprocess.check_output(['git','submodule','status','lib/tinyexpr-plusplus','lib/gnu-units'],cwd=APP,text=True).splitlines(),
 'image_sha256':hashlib.sha256(image).hexdigest(),'app_bytes':(OUT/'firmware.bin').stat().st_size,'factory_partition':factory,
 'sources':{str(p.relative_to(APP)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources if p.is_file()},
 'purpose':'Simulator-only headless speech adaptation of actual Cardulator; no physical installation validation'}
(OUT/'inputs.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in meta.items() if k!='sources'},indent=2))
