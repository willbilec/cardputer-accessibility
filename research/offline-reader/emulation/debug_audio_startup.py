"""Inspect an emulated CPU at the stalled I2S call; never connects to hardware."""
import json
from pathlib import Path
import subprocess
import time
import sys
from run_emulator import ROOT,install,mount

def main():
 out=ROOT/'test-results/emulation';image=out/'advanced-images/board-checkpoints/firmware.factory.bin';elf=image.with_name('firmware.elf')
 debugger=ROOT.parent/'Launcher/.pio/core/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb.exe'
 command=['wsl','--exec','timeout','--kill-after=5s','60s',mount(install()),'--chip','esp32s3','--psram-size','0',
   '--firmware',mount(image),'--elf',mount(elf),'--console','usb-jtag','--timeout','25s','--gdb','3339',
   '--i2c-dev','0:0x34,fill=0','--i2c-dev','0:0x18,fill=0']
 experiment='audio-gdb-clear-update' if '--clear-update' in sys.argv else 'audio-gdb'
 log=out/(experiment+'-emulator.log')
 with log.open('wb') as sink:
  p=subprocess.Popen(command,stdout=sink,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+30
   while time.monotonic()<deadline:
    if 'BOARD_AUDIO after_i2s_std_mode' in log.read_text(encoding='utf-8',errors='replace'):break
    if p.poll() is not None:raise RuntimeError('Emulator ended before checkpoint')
    time.sleep(.1)
   else:raise RuntimeError('Startup checkpoint absent')
   commands=out/(experiment+'-commands.txt')
   intervention='set variable I2S1.tx_conf.tx_update=0\np/x I2S1.tx_conf\n' if '--clear-update' in sys.argv else ''
   commands.write_text('set pagination off\nset remotetimeout 10\ntarget remote localhost:3339\ninfo threads\nthread apply all bt\nthread 2\ninfo registers pc\nx/10i $pc\ninfo symbol $pc\np/x I2S1.tx_conf\n'+intervention+'detach\nquit\n',encoding='utf-8')
   result=subprocess.run([str(debugger),'-batch',str(elf),'-x',str(commands)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
   text=result.stdout.decode('utf-8','replace');(out/(experiment+'.txt')).write_text(text,encoding='utf-8');print(text)
  finally:
   try:p.wait(timeout=40)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 (out/(experiment+'-run.json')).write_text(json.dumps({'command':command,'debugger_intervention':intervention},indent=2)+'\n')

if __name__=='__main__':main()
