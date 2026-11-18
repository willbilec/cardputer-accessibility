"""Reproduce the startup stall with only SDK I2S, on both S3 I2S ports."""
import json
import shutil
import subprocess
from run_emulator import ROOT,run

out=ROOT/'test-results/emulation';build=ROOT/'.pio/build/emulator-i2s-control'
frozen=out/'i2s-sdk-control';frozen.mkdir(exist_ok=True)
for name in ('bootloader.bin','partitions.bin','firmware.bin','firmware.elf'):shutil.copy2(build/name,frozen/name)
core=ROOT.parent/'Launcher/.pio/core'
subprocess.run([str(core/'penv/Scripts/python.exe'),str(core/'packages/tool-esptoolpy/esptool.py'),'--chip','esp32s3',
 'merge-bin','--output',str(frozen/'firmware.factory.bin'),'--flash-mode','dio','--flash-freq','80m','--flash-size','8MB',
 '0x0',str(frozen/'bootloader.bin'),'0x8000',str(frozen/'partitions.bin'),'0x10000',str(frozen/'firmware.bin')],check=True)
summary={}
for port in (0,1):
 output=run('i2s-sdk-control-port'+str(port),frozen/'firmware.factory.bin',frozen/'firmware.elf',duration='10s',wall_timeout=40,
  extra=('--inject-on','I2S_CONTROL_CONFIG','--inject',str(port)+'\n','--exit-on','I2S_CONTROL_COMPLETE'))
 summary[str(port)]={'checkpoints':[s for s in output.splitlines() if s.startswith('I2S_CONTROL')],
  'complete':'I2S_CONTROL_COMPLETE' in output}
 assert f'I2S_CONTROL port={port} new=0' in output and 'I2S_CONTROL std=0' in output
 assert 'I2S_CONTROL before_enable' in output and not summary[str(port)]['complete']
(out/'i2s-sdk-control-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('Both SDK-only ports reproduced the enable stall; expected diagnostic outcome, not an audio pass')
