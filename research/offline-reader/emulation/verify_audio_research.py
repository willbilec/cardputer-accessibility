"""Check saved debugger and SDK-only evidence; no emulator/device interaction."""
from pathlib import Path
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'test-results/emulation'
def read(name):return (out/name).read_text(encoding='utf-8')
base=read('audio-gdb.txt');changed=read('audio-gdb-clear-update.txt')
base_log=read('audio-gdb-emulator.log');changed_log=read('audio-gdb-clear-update-emulator.log')
image=out/'advanced-images/board-checkpoints/firmware.factory.bin'
assert hashlib.sha256(image.read_bytes()).hexdigest()=='ff8cf094459ca52e98ea3664a07cc619eece622c524c221f0854daa8759c45e3'
for text in (base,changed):
 assert 'i2s_ll_tx_update' in text and 'i2s_channel_enable' in text and 'tx_update = 0x1' in text
assert 'BOARD_PROBE_COMPLETE' not in base_log
assert 'BOARD_AUDIO after_i2s_enable' in changed_log and 'BOARD_PROBE audio=1' in changed_log and 'BOARD_PROBE_COMPLETE' in changed_log
assert 'tx_update = 0x0' in changed and 'tx_start = 0x1' in changed
commands=read('audio-gdb-clear-update-commands.txt')
assert 'set variable I2S1.tx_conf.tx_update=0' in commands
assert 'set variable' not in read('audio-gdb-commands.txt')
controls=json.loads(read('i2s-sdk-control-summary.json'))
for port in (0,1):
 log=read(f'i2s-sdk-control-port{port}.log')
 assert f'I2S_CONTROL port={port} new=0' in log and 'I2S_CONTROL std=0' in log
 assert 'I2S_CONTROL before_enable' in log and 'I2S_CONTROL_COMPLETE' not in log
 assert not controls[str(port)]['complete']
result={'same_original_image_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
 'original_stall_observed':True,'sdk_update_wait_observed':True,
 'emulated_update_bit_clear_allows_initialization':True,'sdk_only_ports_reproduce_stall':[0,1],
 'physical_audio_verified':False,'pcm_dma_playback_verified':False}
(out/'audio-research-validated.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
