"""Build a standalone diagnostic image; does not modify SD apps or flash hardware."""
from pathlib import Path
import argparse,hashlib,json,runpy,shutil,subprocess,sys
args=argparse.ArgumentParser();args.add_argument('--coexist',action='store_true');args=args.parse_args()
environment='backgroundaudio-coexist' if args.coexist else 'backgroundaudio-minimal'
ROOT=Path(__file__).resolve().parents[1];out=ROOT/('test-results/backgroundaudio-coexist' if args.coexist else 'test-results/backgroundaudio');out.mkdir(exist_ok=True)
runpy.run_path(str(ROOT/'prepare_background.py'))['prepare']()
if args.coexist:runpy.run_path(str(ROOT/'emulation/prepare_coexist_music.py'))['prepare'](ROOT)
result=subprocess.run([sys.executable,'-m','platformio','run','-e',environment,'-j','6'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'build.log').write_bytes(result.stdout)
if result.returncode:raise SystemExit(result.returncode)
build=ROOT/'.pio/build'/environment;core=ROOT.parent/'Launcher/.pio/core'
for name in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin'):shutil.copy2(build/name,out/name)
target=out/'backgroundaudio-simulator.bin'
subprocess.run([str(core/'penv/Scripts/python.exe'),str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3','merge-bin','--output',str(target),
 '--flash-mode','dio','--flash-freq','80m','--flash-size','8MB','0x0',str(out/'bootloader.bin'),'0x8000',str(out/'partitions.bin'),'0x10000',str(out/'firmware.bin')],check=True)
meta={'environment':environment,'upstream_commit':'94641100866beae2cc97dc925c0d4562ad168879','version':'1.4.4','buffers':2 if args.coexist else 1,'phoneme_limit':250,'optimization':'O3',
 'app_bytes':(out/'firmware.bin').stat().st_size,'image_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'purpose':'Simulator-only diagnostic, not a physical installation image'}
assert meta['app_bytes']<=0x280000
(out/'inputs.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta,indent=2))
