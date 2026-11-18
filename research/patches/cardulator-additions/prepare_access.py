"""Guard and adapt the user-tested speech/keypad service without editing CardEX."""
from pathlib import Path
import hashlib,runpy,json
if '__file__' in globals():ROOT=Path(__file__).resolve().parent
else:
 Import('env')
 ROOT=Path(env['PROJECT_DIR'])
PINS={
 'Reader.h':'f3cdc7fe5d8ed55f4a8a880730c58fdddb366cc886d7a7c25090ba23e352164c',
 'Reader.cpp':'fa1f8de9ca40936e73443f05a35b0d357fd8f39d787fa46d80773413983e491e',
 'ReaderChunks.h':'cb4aaca5cb8a82174e1f544cd8e372a8608974984dad0f426ead8cdf0ea88fa9',
 'SimulatorKeyboard.h':'518886f28c5f86f62fa448a1452c63f031081125612b9ce6a7be6c1c66626c9d',
}
def prepare():
 runpy.run_path(str(ROOT.parent/'offline-reader/prepare_background.py'))['prepare']()
 source=ROOT.parent/'CardEX/src';out=ROOT/'src/generated_access';out.mkdir(parents=True,exist_ok=True)
 for name,digest in PINS.items():
  raw=(source/name).read_bytes();assert hashlib.sha256(raw).hexdigest()==digest,name+' changed: review before reusing'
  text=raw.decode('utf-8').replace('\r\n','\n').replace('CARDEX_EMULATOR_DEMO','CARDULATOR_EMULATOR').replace('CARDEX_SPEECH','CARDULATOR_SPEECH').replace('CARDEX_','CARDULATOR_')
  target=out/name;adapted=text.encode('utf-8')
  if not target.exists() or target.read_bytes()!=adapted:target.write_bytes(adapted)
 (out/'sources.json').write_text(json.dumps(PINS,indent=2)+'\n',encoding='utf-8')
 # The complete catalogue fatally exhausts RAM alongside speech on this target.
 # Compile a separate bounded catalogue only in this explicit access profile.
 data=(ROOT/'accessibility_units.dat').read_bytes()
 compact='#include "../embedded_units_dat.h"\nconst char embedded_units_dat[] = {'+','.join(str(b) for b in data)+',0};\nconst size_t embedded_units_dat_len = '+str(len(data))+';\n'
 target=out/'compact_units.c';adapted=compact.encode('utf-8')
 if not target.exists() or target.read_bytes()!=adapted:target.write_bytes(adapted)
 return out
if __name__=='__main__':print(prepare())
else:
 Import('env')
 if env['PIOENV']=='cardulator-access':
  out=prepare();env.Append(CPPPATH=[str(out)])
