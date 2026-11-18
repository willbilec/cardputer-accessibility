"""Run diagnostic images, save raw evidence, never flash a physical device."""
import json
import re
import sys
import shutil
import subprocess
import hashlib
from run_emulator import ROOT,run

def main():
 results={}
 cases=[
  ('stress-normal','emulator-stress','STRESS_COMPLETE','190s',600,()),
  ('stress-reserve64k','emulator-stress','STRESS_COMPLETE','190s',600,('--inject-on','STRESS_CONFIG','--inject','65536\n')),
  ('ble-speech','emulator-ble','BLE_COMPLETE','100s',120,()),
  ('board-checkpoints','emulator-board-probe','BOARD_PROBE_COMPLETE','15s',60,('--i2c-dev','0:0x34,fill=0','--i2c-dev','0:0x18,fill=0')),
 ]
 selected=set(sys.argv[1:])
 for name,env,marker,duration,wall,extra in cases:
  if selected and name not in selected:continue
  build=ROOT/'.pio/build'/env
  frozen=ROOT/'test-results/emulation/advanced-images'/name
  frozen.mkdir(parents=True,exist_ok=True)
  for artifact in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin'):
   shutil.copy2(build/artifact,frozen/artifact)
  # Regenerate merged images explicitly: cached builds need not retain one.
  # Diagnostic apps fit the factory slot; no OTA metadata or device is used.
  esptool=ROOT.parent/'Launcher/.pio/core/packages/tool-esptoolpy/esptool.py'
  tool_python=ROOT.parent/'Launcher/.pio/core/penv/Scripts/python.exe'
  subprocess.run([str(tool_python if tool_python.exists() else sys.executable),str(esptool),'--chip','esp32s3','merge-bin','--output',str(frozen/'firmware.factory.bin'),
      '--flash-mode','dio','--flash-freq','80m','--flash-size','8MB',
      '0x0',str(frozen/'bootloader.bin'),'0x8000',str(frozen/'partitions.bin'),'0x10000',str(frozen/'firmware.bin')],check=True)
  output=run('advanced-'+name,frozen/'firmware.factory.bin',frozen/'firmware.elf',
      extra=('--exit-on',marker,*extra),duration=duration,wall_timeout=wall)
  failures=[s for s in output.splitlines() if any(x in s for x in ('BENCH_FAIL','STRESS_FAIL','BLE_FAIL','KLATT_ERROR','Guru Meditation','panicked','Exception (','coprocessor exception'))]
  if name.startswith('stress'):
   reserve=65536 if name=='stress-reserve64k' else 0
   if f'STRESS_BEGIN reserve={reserve} psram=0' not in output:failures.append('Incorrect reserve/PSRAM configuration')
   counts=re.search(r'STRESS_COMPLETE actions=(\d+) stops=(\d+) repeats=(\d+) samples=(\d+) music=(\d+)',output)
   if not counts or any(int(n)<minimum for n,minimum in zip(counts.groups(),(850,100,60,1000000,7000000))):failures.append('Stress workload counts below required minimum')
   if not re.search(r'STRESS_PROGRESS .*wifi=3 ',output):failures.append('Wi-Fi connected checkpoint absent')
  metadata=json.loads((ROOT/'test-results/emulation'/('advanced-'+name+'.json')).read_text())
  complete=marker in output and not failures and metadata['exit_code']==0 and not metadata['wall_timeout']
  results[name]={'complete':complete,'failures':failures,
      'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'src').glob('*.cpp')},
      'checkpoints':[s for s in output.splitlines() if s.startswith(('STRESS_','BLE_','BOARD_PROBE','BOARD_AUDIO','BENCH_MEM'))]}
  latency=[int(x) for x in re.findall(r'^FIRST_PCM latency_ms=(\d+)',output,re.M)]
  if latency:
   latency.sort();results[name]['emulated_enqueue_to_pcm_ms']={'count':len(latency),'minimum':latency[0],'median':latency[len(latency)//2],'p95':latency[min(len(latency)-1,int(len(latency)*.95))],'maximum':latency[-1]}
  print(name+(': COMPLETE' if complete else ': INCOMPLETE'),flush=True)
  path=ROOT/'test-results/emulation/advanced-summary.json'
  previous=json.loads(path.read_text()) if path.exists() else {}
  previous.update(results);path.write_text(json.dumps(previous,indent=2)+'\n')
 if not all(r['complete'] for r in results.values()):raise SystemExit(1)

if __name__=='__main__':main()
