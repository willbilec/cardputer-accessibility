"""Adapt pinned geo-tp transport sources, leaving its checkout untouched."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parent
HASHES={
 "bluetooth.cpp":"77131845dd5dc15690b19cb723e5195010d0ddad765afe957e87f4585e099e79",
 "bluetooth.h":"14f962aac1d430ad676d5b9549019dadf0f4a809ffb6aa3d9115cd82aaa2c81e",
 "usbHid.cpp":"b60ef56c2acdef1e9797e5b6622dbbc4ecce4487abd66002481f5ac0f078f255",
 "usbHid.h":"bc82d9b923cefd4c56df8aa4b2b4a02ee49f119e88bc48c5fbaecee7e0ef83df"}
def body_replace(text, signature, body):
 start=text.index(signature); opening=text.index('{',start); depth=1; pos=opening+1
 while depth:
  if text[pos]=='{':depth+=1
  elif text[pos]=='}':depth-=1
  pos+=1
 return text[:opening]+'{\n'+body+'\n}'+text[pos:]

def prepare():
 src=ROOT.parent/'Bluetooth-Keyboard-Mouse-Emulator/src'; output=ROOT/'generated'; output.mkdir(exist_ok=True)
 files={}
 for name,digest in HASHES.items():
  raw=(src/name).read_bytes().replace(b'\r\n',b'\n')
  if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('Unreviewed HID source: '+name)
  files[name]=raw.decode()
 bleh=files['bluetooth.h'].replace('#include "display.h"','#include "headless_board.h"\n#include <atomic>')
 bleh=bleh.replace('extern bool isConnected;','')
 bleh=bleh.replace('#include "HIDKeyboardTypes.h"','').replace('#include "HIDTypes.h"','')
 bleh=bleh.replace('BLEServer* pServer, esp_ble_gatts_cb_param_t *param','BLEServer* pServer')
 # The upstream sends four bytes but originally describes only buttons/X/Y.
 bleh=bleh.replace('0x09, 0x31,','0x09, 0x31,',1)
 anchor='0x09, 0x31,        //     Usage (Y)'
 assert anchor in bleh
 bleh=bleh.replace(anchor,anchor+'\n    0x09, 0x38,        //     Usage (Wheel)',1)
 bleh=bleh.replace('0x95, 0x02,        //     Report Count (2)','0x95, 0x03,        //     Report Count (3)',1)
 ble=files['bluetooth.cpp'].replace('#include "bluetooth.h"','#include "bluetooth_generated_api.h"\n#include "hid_reports.h"')
 ble=ble.replace('bool bluetoothIsConnected = false;','std::atomic<bool> bluetoothIsConnected{false};\nstatic bool bluetoothReady=false;\nbool bluetoothTransportReady(){return bluetoothReady;}')
 ble=ble.replace('BLEDevice::init("M5-Keyboard-Mouse");','if(!BLEDevice::init("M5-Keyboard-Mouse")) return;')
 ble=ble.replace('BLEServer *pServer = BLEDevice::createServer();','BLEServer *pServer = BLEDevice::createServer();\n    if(!pServer) return;')
 ble=ble.replace('pAdvertising->start();','bool advertisingStarted=pAdvertising->start();')
 security='pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);'
 assert security in ble
 ble=ble.replace(security,security+'\n    bluetoothReady=advertisingStarted;')
 ble=ble.replace('BLEServer* pServer, esp_ble_gatts_cb_param_t *param','BLEServer* pServer')
 ble=ble.replace('    pServer->disconnect(param->disconnect.conn_id); // Properly disconnect the client','    // Already disconnected. Restart advertising without a second disconnect.')
 ble=ble.replace('setAppearance(HID_MOUSE)','setAppearance(0x03C2)')
 ble=body_replace(ble,'void bluetoothKeyboard()', '''auto status=M5Cardputer.Keyboard.keysState();
    auto report=hidReports::keyboard(status.modifiers,status.hid_keys);
    keyboardInput->setValue(report.data(),report.size()); keyboardInput->notify();''')
 usb=files['usbHid.cpp'].replace('#include "usbHid.h"','#include "usb_generated_api.h"\n#include "hid_reports.h"\n#include <USB.h>\n#include <tusb.h>')
 usb=body_replace(usb,'void usbMouse()', '''auto &keys=M5Cardputer.Keyboard;
    auto status=keys.keysState();
    if(status.enter) mouse.press(MOUSE_BUTTON_LEFT); else mouse.release(MOUSE_BUTTON_LEFT);
    if(keys.isKeyPressed('\\\\')) mouse.press(MOUSE_BUTTON_RIGHT); else mouse.release(MOUSE_BUTTON_RIGHT);
    mouse.move(int(keys.isKeyPressed('/'))-int(keys.isKeyPressed(',')),
               int(keys.isKeyPressed('.'))-int(keys.isKeyPressed(';')));''')
 usb=body_replace(usb,'void usbKeyboard()', '''if(!M5Cardputer.Keyboard.isChange()) return;
    const auto &state=M5Cardputer.Keyboard.keysState();
    auto bytes=hidReports::keyboard(state.modifiers,state.hid_keys);
    KeyReport report{}; report.modifiers=bytes[0];
    for(unsigned i=0;i<6;++i) report.keys[i]=bytes[2+i];
    keyboard.sendReport(&report);''')
 usb+='''
bool initUsbHid() { mouse.begin(); keyboard.begin(); return USB.begin(); }
bool usbHostConnected() { return tud_mounted(); }
void releaseHidReports(bool usb) {
 if(usb) { keyboard.releaseAll(); mouse.release(MOUSE_BUTTON_LEFT); mouse.release(MOUSE_BUTTON_RIGHT); }
 else if(getBluetoothStatus()) sendEmptyReports();
}
'''
 usbh=files['usbHid.h'].replace('#include <M5Cardputer.h>','#include "headless_board.h"')
 (output/'bluetooth_generated_api.h').write_text(bleh,encoding='utf-8')
 (output/'usb_generated_api.h').write_text(usbh,encoding='utf-8')
 (output/'usb_transport_generated.h').write_text(usb,encoding='utf-8')
 (output/'bluetooth_transport_generated.h').write_text(ble,encoding='utf-8')
 (output/'hid_transport_generated.h').write_text(ble+'\n'+usb,encoding='utf-8')
if __name__=='__main__':prepare()
