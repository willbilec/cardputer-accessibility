"""Validate completed run evidence without rerunning the CPU workloads."""
from pathlib import Path
import hashlib
import json
import math
import re

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'test-results/emulation'

def main():
 summary={}
 for name,reserve in [('stress-normal',0),('stress-reserve64k',65536)]:
  raw=(OUT/f'advanced-{name}.log').read_text(encoding='utf-8')
  meta=json.loads((OUT/f'advanced-{name}.json').read_text())
  image=OUT/'advanced-images'/name/'firmware.factory.bin'
  assert hashlib.sha256(image.read_bytes()).hexdigest()==meta['image_sha256']
  assert meta['exit_code']==0 and not meta['wall_timeout']
  assert f'STRESS_BEGIN reserve={reserve} psram=0' in raw
  assert not any(s in raw for s in ['STRESS_FAIL','BENCH_FAIL','KLATT_ERROR','Guru Meditation'])
  m=re.search(r'STRESS_COMPLETE actions=(\d+) stops=(\d+) repeats=(\d+) samples=(\d+) music=(\d+).*warm_heap=(\d+) warm_largest=(\d+) end_heap=(\d+) end_largest=(\d+)',raw)
  assert m
  counts=list(map(int,m.groups()))
  assert all(a>=b for a,b in zip(counts[:5],[850,100,60,1000000,7000000]))
  assert 'STRESS_WIFI on' in raw and 'STRESS_WIFI off' in raw
  assert re.search(r'STRESS_PROGRESS .*wifi=3 ',raw)
  timing=sorted(map(int,re.findall(r'^FIRST_PCM latency_ms=(\d+)',raw,re.M)))
  assert len(timing)>=700
  memory=[tuple(map(int,m)) for m in re.findall(r'^BENCH_MEM stage=\S+ free=(\d+) minimum=(\d+) largest=(\d+) loop_stack=(\d+)',raw,re.M)]
  assert memory
  stacks=list(map(int,re.findall(r'speech_stack=(\d+)',raw)))
  summary[name]={
   'image_sha256':meta['image_sha256'],'extra_reserve_bytes':reserve,'emulated_duration_seconds':180,
   'actions':counts[0],'stops':counts[1],'repeats':counts[2],'speech_samples':counts[3],'music_frames':counts[4],
   'minimum_free_heap_bytes':min(m[1] for m in memory),
   'smallest_observed_largest_block_bytes':min(m[2] for m in memory),
   'warm_heap_bytes':counts[5],'end_heap_bytes':counts[7],'end_largest_block_bytes':counts[8],
   'minimum_reported_speech_stack_bytes':min(stacks),
   'emulated_enqueue_to_pcm_ms':{'count':len(timing),'minimum':timing[0],'median':timing[len(timing)//2],
       'p95_nearest_rank':timing[math.ceil(len(timing)*.95)-1],'maximum':timing[-1]},
   'not_hardware_timing':True}
 for name in ('ble-speech','board-checkpoints'):
  raw=(OUT/f'advanced-{name}.log').read_text(encoding='utf-8')
  meta=json.loads((OUT/f'advanced-{name}.json').read_text())
  image=OUT/'advanced-images'/name/'firmware.factory.bin'
  assert hashlib.sha256(image.read_bytes()).hexdigest()==meta['image_sha256']
  marker='BLE_COMPLETE' if name=='ble-speech' else 'BOARD_PROBE_COMPLETE'
  assert marker not in raw
  summary[name]={'complete':False,'image_sha256':meta['image_sha256'],
      'last_checkpoint':[s for s in raw.splitlines() if s.startswith(('BLE_CHECKPOINT','BOARD_AUDIO'))][-1]}
 (OUT/'advanced-validated.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
