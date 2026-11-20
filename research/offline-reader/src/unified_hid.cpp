#include "unified_hid.h"
#if defined(UNIFIED_CUSTOM_BUILD) && !defined(UNIFIED_KEYBOARD_APP)
#include <USB.h>
#include <tusb.h>

bool startUnifiedUSB(){return USB.begin();}
bool unifiedUSBConnected(){return tud_mounted();}
#else
#include <BLEDevice.h>
#include <Preferences.h>
#include "bluetooth_generated_api.h"
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <tusb.h>

namespace {
char deviceName[23]="Cardputer ADV KB";
int activeSlot=1;
constexpr uint16_t noConnection=UINT16_MAX;
USBHIDKeyboard usbKeyboard;
USBHIDMouse usbMouse;
bool radio=false, enabled=false, pairing=false, authenticated=false, accepting=false;
uint32_t stopStarted=0;
uint32_t securityStarted=0;
uint16_t connection=noConnection, mouseReport=0, keyboardReport=0;
std::atomic<uint16_t> keyboardSubscriber{noConnection}, mouseSubscriber{noConnection};
std::atomic<bool> allowRepair{false};
std::atomic<int> batteryPercent{-1};
uint8_t mouseButtons=0, ledState=0;
String selectedPeer;
portMUX_TYPE slotLock=portMUX_INITIALIZER_UNLOCKED;
char otherSlotPeers[2][15]{};
enum class EventType : uint8_t { Connect, Disconnect, Encrypted, Rejected, LinkError };
struct Event { EventType type; uint16_t handle; ble_addr_t address; };
QueueHandle_t events=nullptr;

enum class Value : uintptr_t { Map, Info, Control, Mouse, Keyboard, Led, MouseRef, KeyboardRef, LedRef, Pnp, Battery };
constexpr ble_uuid16_t hidUuid = BLE_UUID16_INIT(0x1812);
constexpr ble_uuid16_t deviceInfoUuid = BLE_UUID16_INIT(0x180a);
constexpr ble_uuid16_t mapUuid = BLE_UUID16_INIT(0x2a4b);
constexpr ble_uuid16_t infoUuid = BLE_UUID16_INIT(0x2a4a);
constexpr ble_uuid16_t controlUuid = BLE_UUID16_INIT(0x2a4c);
constexpr ble_uuid16_t reportUuid = BLE_UUID16_INIT(0x2a4d);
constexpr ble_uuid16_t refUuid = BLE_UUID16_INIT(0x2908);
constexpr ble_uuid16_t pnpUuid = BLE_UUID16_INIT(0x2a50);
constexpr ble_uuid16_t batteryServiceUuid = BLE_UUID16_INIT(0x180f);
constexpr ble_uuid16_t batteryLevelUuid = BLE_UUID16_INIT(0x2a19);

int access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *arg){
 const auto value=static_cast<Value>(reinterpret_cast<uintptr_t>(arg));
 static constexpr uint8_t info[]{0x11,0x01,0,0x02};
 static constexpr uint8_t mouseRef[]{1,1}, keyboardRef[]{2,1}, ledRef[]{2,2};
 static constexpr uint8_t pnp[]{0x02,0x34,0x12,0x78,0x56,0x03,0x01};
 static constexpr uint8_t emptyKeyboard[8]{}, emptyMouse[4]{};
 if(ctx->op==BLE_GATT_ACCESS_OP_WRITE_CHR){
  if(OS_MBUF_PKTLEN(ctx->om)!=1)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  if(value==Value::Led)os_mbuf_copydata(ctx->om,0,1,&ledState);
  return 0;
 }
 const uint8_t *bytes=nullptr;
 size_t size=0;
 switch(value){
 case Value::Map:bytes=HID_REPORT_MAP;size=sizeof(HID_REPORT_MAP);break;
 case Value::Info:bytes=info;size=sizeof(info);break;
 case Value::Mouse:bytes=emptyMouse;size=sizeof(emptyMouse);break;
 case Value::Keyboard:bytes=emptyKeyboard;size=sizeof(emptyKeyboard);break;
 case Value::Led:bytes=&ledState;size=1;break;
 case Value::MouseRef:bytes=mouseRef;size=sizeof(mouseRef);break;
 case Value::KeyboardRef:bytes=keyboardRef;size=sizeof(keyboardRef);break;
 case Value::LedRef:bytes=ledRef;size=sizeof(ledRef);break;
 case Value::Pnp:bytes=pnp;size=sizeof(pnp);break;
 case Value::Battery:{
  int percent=batteryPercent;
  if(percent<0)return BLE_ATT_ERR_UNLIKELY;
  uint8_t level=percent;
  return os_mbuf_append(ctx->om,&level,1)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
 }
 default:return BLE_ATT_ERR_READ_NOT_PERMITTED;
 }
 return os_mbuf_append(ctx->om,bytes,size)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}

void *attribute(Value value){return reinterpret_cast<void *>(uintptr_t(value));}
void describe(ble_gatt_chr_def &chr,const ble_uuid16_t &uuid,uint16_t flags,Value value,
              ble_gatt_dsc_def *desc=nullptr,uint16_t *handle=nullptr){
 chr.uuid=&uuid.u;chr.flags=flags;chr.access_cb=access;chr.arg=attribute(value);
 chr.descriptors=desc;chr.val_handle=handle;
}

bool startServices(){
 static ble_gatt_dsc_def refs[3][2]{};
 static ble_gatt_chr_def hid[7]{},deviceInfo[2]{},battery[2]{};
 static ble_gatt_svc_def services[4]{};
 for(int i=0;i<3;++i){
  refs[i][0].uuid=&refUuid.u;refs[i][0].att_flags=BLE_ATT_F_READ;
  refs[i][0].access_cb=access;refs[i][0].arg=attribute(static_cast<Value>(uintptr_t(Value::MouseRef)+i));
 }
 describe(hid[0],mapUuid,BLE_GATT_CHR_F_READ,Value::Map);
 describe(hid[1],infoUuid,BLE_GATT_CHR_F_READ,Value::Info);
 describe(hid[2],controlUuid,BLE_GATT_CHR_F_WRITE_NO_RSP,Value::Control);
 constexpr uint16_t inputFlags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_NOTIFY;
 describe(hid[3],reportUuid,inputFlags,Value::Mouse,refs[0],&mouseReport);
 describe(hid[4],reportUuid,inputFlags,Value::Keyboard,refs[1],&keyboardReport);
 describe(hid[5],reportUuid,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE|
          BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_NO_RSP,Value::Led,refs[2]);
 describe(deviceInfo[0],pnpUuid,BLE_GATT_CHR_F_READ,Value::Pnp);
 describe(battery[0],batteryLevelUuid,BLE_GATT_CHR_F_READ,Value::Battery);
 services[0].type=BLE_GATT_SVC_TYPE_PRIMARY;services[0].uuid=&hidUuid.u;services[0].characteristics=hid;
 services[1].type=BLE_GATT_SVC_TYPE_PRIMARY;services[1].uuid=&deviceInfoUuid.u;services[1].characteristics=deviceInfo;
 services[2].type=BLE_GATT_SVC_TYPE_PRIMARY;services[2].uuid=&batteryServiceUuid.u;services[2].characteristics=battery;
 ble_svc_gap_init();ble_svc_gatt_init();
 return ble_svc_gap_device_name_set(deviceName)==0&&
        ble_gatts_count_cfg(services)==0&&ble_gatts_add_svcs(services)==0&&ble_gatts_start()==0;
}

void formatPeerKey(const ble_addr_t &addr,char (&value)[15]){
 snprintf(value,sizeof(value),"%02x%02x%02x%02x%02x%02x%02x",
          addr.type&1,addr.val[0],addr.val[1],addr.val[2],addr.val[3],addr.val[4],addr.val[5]);
}
String peerKey(const ble_addr_t &addr){
 char value[15];
 formatPeerKey(addr,value);
 return value;
}

bool belongsToOtherSlot(const ble_addr_t &addr){
 char key[15];
 formatPeerKey(addr,key);
 portENTER_CRITICAL(&slotLock);
 bool matches=!memcmp(key,otherSlotPeers[0],sizeof(key))||!memcmp(key,otherSlotPeers[1],sizeof(key));
 portEXIT_CRITICAL(&slotLock);
 return matches;
}

int gapEvent(ble_gap_event *event,void *){
 Event result{};
 ble_gap_conn_desc desc{};
 switch(event->type){
 case BLE_GAP_EVENT_CONNECT:
  if(event->connect.status){result={EventType::LinkError,uint16_t(event->connect.status),{}};break;}
  if(ble_gap_conn_find(event->connect.conn_handle,&desc))return 0;
  keyboardSubscriber=mouseSubscriber=noConnection;
  result={EventType::Connect,desc.conn_handle,desc.peer_id_addr};break;
 case BLE_GAP_EVENT_DISCONNECT:
  keyboardSubscriber=mouseSubscriber=noConnection;
  result={EventType::Disconnect,event->disconnect.conn.conn_handle,{}};break;
 case BLE_GAP_EVENT_ENC_CHANGE:
  if(ble_gap_conn_find(event->enc_change.conn_handle,&desc))return 0;
  result={event->enc_change.status==0&&desc.sec_state.encrypted?EventType::Encrypted:EventType::Rejected,
          desc.conn_handle,desc.peer_id_addr};break;
 case BLE_GAP_EVENT_SUBSCRIBE:
  if(event->subscribe.attr_handle==keyboardReport)keyboardSubscriber=event->subscribe.cur_notify?event->subscribe.conn_handle:noConnection;
  if(event->subscribe.attr_handle==mouseReport)mouseSubscriber=event->subscribe.cur_notify?event->subscribe.conn_handle:noConnection;
  return 0;
 case BLE_GAP_EVENT_REPEAT_PAIRING:
  if(!allowRepair||ble_gap_conn_find(event->repeat_pairing.conn_handle,&desc))return BLE_GAP_REPEAT_PAIRING_IGNORE;
  if(belongsToOtherSlot(desc.peer_id_addr))return BLE_GAP_REPEAT_PAIRING_IGNORE;
  if(ble_store_util_delete_peer(&desc.peer_id_addr))return BLE_GAP_REPEAT_PAIRING_IGNORE;
  return BLE_GAP_REPEAT_PAIRING_RETRY;
 default:return 0;
 }
 if(xQueueSend(events,&result,0)!=pdTRUE&&result.type!=EventType::LinkError)
  ble_gap_terminate(result.handle,BLE_ERR_REM_USER_CONN_TERM);
 return 0;
}

bool startAdvertising(){
 uint8_t mac[6]{};
 if(esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK)return false;
 uint64_t hash=14695981039346656037ULL;
 for(uint8_t byte:mac)hash=(hash^byte)*1099511628211ULL;
 // Bluetooth static random addresses reserve bits 47–46 as 11. Put the
 // slot in bits 1–0 so one Cardputer's three addresses are distinct.
 hash=((hash&((1ULL<<44)-1))<<2)|unsigned(activeSlot-1);
 if(hash==0||hash==((1ULL<<46)-1))hash^=4;
 uint8_t address[6];
 for(size_t i=0;i<sizeof(address);++i)address[i]=hash>>(8*i);
 address[5]|=0xc0;
 if(ble_hs_id_set_rnd(address))return false;
 const String name=keyboardDeviceName(activeSlot);
 name.toCharArray(deviceName,sizeof(deviceName));
 if(ble_svc_gap_device_name_set(deviceName))return false;
 ble_hs_adv_fields fields{};
 fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 fields.name=reinterpret_cast<const uint8_t *>(deviceName);
 fields.name_len=strlen(deviceName);fields.name_is_complete=1;
 auto uuid=hidUuid;
 fields.uuids16=&uuid;fields.num_uuids16=1;fields.uuids16_is_complete=1;
 if(ble_gap_adv_set_fields(&fields))return false;
 ble_hs_adv_fields scan{};
 scan.appearance=0x03c1;scan.appearance_is_present=1;
 if(ble_gap_adv_rsp_set_fields(&scan))return false;
 ble_gap_adv_params params{};
 params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;
 params.itvl_min=BLE_GAP_ADV_ITVL_MS(30);params.itvl_max=BLE_GAP_ADV_ITVL_MS(50);
 if(!pairing){
  // Saved keys encode address type followed by the six NimBLE address bytes.
  ble_addr_t target{};
  unsigned bytes[7]{};
  int consumed=0;
  if(sscanf(selectedPeer.c_str(),"%2x%2x%2x%2x%2x%2x%2x%n",
            &bytes[0],&bytes[1],&bytes[2],&bytes[3],&bytes[4],&bytes[5],&bytes[6],&consumed)!=7||
     consumed!=14||selectedPeer.length()!=14||bytes[0]>1)return false;
  target.type=bytes[0];
  for(size_t i=0;i<sizeof(target.val);++i)target.val[i]=bytes[i+1];
  int status=ble_gap_wl_set(&target,1);
#ifdef UNIFIED_DIAGNOSTICS
  if(status){Serial.printf("UF_BLE slot filter failed=%d\n",status);return false;}
#else
  if(status){return false;}
#endif
  params.filter_policy=BLE_HCI_ADV_FILT_CONN;
 }
 return ble_gap_adv_start(BLE_OWN_ADDR_RANDOM,nullptr,BLE_HS_FOREVER,&params,gapEvent,nullptr)==0;
}

bool migrateLegacyBonds(){
 Preferences profile;
 if(!profile.begin("kbdSlots",false))return false;
 if(profile.getBool("separateIds",false))return true;
 // The old public identity's bonds cannot reconnect to the new slots.
 return ble_store_clear()==0&&profile.putBool("separateIds",true)==1;
}

void disconnect(uint16_t handle){ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);}

void shutdownRadio(){
 BLEDevice::deinit(false);
 radio=false;
 connection=noConnection;
 stopStarted=0;
 securityStarted=0;
 xQueueReset(events);
}

void notify(uint16_t handle,uint16_t subscriber,uint8_t *data,size_t size){
 if(!authenticated||connection==noConnection||subscriber!=connection)return;
 if(auto *packet=ble_hs_mbuf_from_flat(data,size))ble_gatts_notify_custom(connection,handle,packet);
}
}

bool startUnifiedUSB(){
 USB.productName("Cardputer ADV");
 usbKeyboard.begin();usbMouse.begin();
 return USB.begin();
}

bool unifiedUSBConnected(){return tud_mounted();}
bool keyboardRadioRunning(){return radio;}
void setHidBatteryLevel(int percent){batteryPercent=percent;}
String keyboardDeviceName(int slot){
 uint8_t address[6]{};
 char label[23];
 if(esp_read_mac(address,ESP_MAC_BT)==ESP_OK){
  snprintf(label,sizeof(label),"Cardputer KB %d %02X%02X",slot,address[4],address[5]);
 }else{
  snprintf(label,sizeof(label),"Cardputer KB %d",slot);
 }
 return label;
}

void stopKeyboardSlot(){
 enabled=authenticated=accepting=false;
 allowRepair=false;
 securityStarted=0;
 if(!radio)return;
 stopStarted=millis()|1;
 if(ble_gap_adv_active())ble_gap_adv_stop();
 if(connection!=noConnection)disconnect(connection);
}

KeyboardStartup startKeyboardSlot(int slot,const String &peer,bool pair,const String (&otherPeers)[2]){
 stopKeyboardSlot();
 char reserved[2][15]{};
 for(int i=0;i<2;++i)if(otherPeers[i].length()==14)memcpy(reserved[i],otherPeers[i].c_str(),14);
 portENTER_CRITICAL(&slotLock);
 memcpy(otherSlotPeers,reserved,sizeof(otherSlotPeers));
 portEXIT_CRITICAL(&slotLock);
 if(!radio){
  if(ESP.getFreeHeap()<68000||heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<20000)
   return KeyboardStartup::InsufficientMemory;
  if(!events)events=xQueueCreate(12,sizeof(Event));
  if(!events)return KeyboardStartup::InsufficientMemory;
#ifdef UNIFIED_DIAGNOSTICS
  Serial.println("UF_BLE controller");
#endif
  if(!BLEDevice::init(keyboardDeviceName(slot)))return KeyboardStartup::Controller;
  radio=true;
#ifdef UNIFIED_DIAGNOSTICS
  Serial.printf("UF_BLE services heap=%u\n",ESP.getFreeHeap());
#endif
  ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding=1;
  ble_hs_cfg.sm_mitm=0;
  ble_hs_cfg.sm_sc=1;
  ble_hs_cfg.sm_our_key_dist=ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
  if(!migrateLegacyBonds()||!startServices()){shutdownRadio();return KeyboardStartup::Services;}
 }
 activeSlot=slot;
 selectedPeer=peer;pairing=pair;allowRepair=pair;stopStarted=0;
 enabled=connection!=noConnection||startAdvertising();
 return enabled?KeyboardStartup::Ready:KeyboardStartup::Advertising;
}

bool pollKeyboardSlot(String &pairedPeer){
 Event event{};
 while(radio&&xQueueReceive(events,&event,0)==pdTRUE){
  switch(event.type){
  case EventType::Connect:
   connection=event.handle;authenticated=false;
   // Android can connect using a private address. Check the peer identity
   // after bond encryption rather than rejecting the initial address.
   accepting=enabled&&!belongsToOtherSlot(event.address);
#ifdef UNIFIED_DIAGNOSTICS
   if(accepting){securityStarted=millis()|1;Serial.println("UF_BLE link opened");}
#else
   if(accepting){securityStarted=millis()|1;}
#endif
   if(!accepting)disconnect(connection);
   break;
  case EventType::Encrypted:{
   if(!accepting||connection!=event.handle)break;
   securityStarted=0;
   if(belongsToOtherSlot(event.address)){disconnect(connection);break;}
   String peer=peerKey(event.address);
#ifdef UNIFIED_DIAGNOSTICS
   if(!pairing&&selectedPeer!=peer){Serial.println("UF_BLE encrypted peer differs from slot");disconnect(connection);break;}
#else
   if(!pairing&&selectedPeer!=peer){disconnect(connection);break;}
#endif
   if(pairing)pairedPeer=selectedPeer=peer;
   pairing=false;allowRepair=false;authenticated=true;
#ifdef UNIFIED_DIAGNOSTICS
   Serial.println("UF_BLE authenticated");
#endif
   break;
  }
  case EventType::Rejected:
#ifdef UNIFIED_DIAGNOSTICS
   if(connection==event.handle){Serial.println("UF_BLE encryption failed");disconnect(connection);}
#else
   if(connection==event.handle){disconnect(connection);}
#endif
   break;
  case EventType::Disconnect:
#ifdef UNIFIED_DIAGNOSTICS
   if(connection==event.handle){connection=noConnection;securityStarted=0;authenticated=accepting=false;Serial.println("UF_BLE disconnected");}
#else
   if(connection==event.handle){connection=noConnection;securityStarted=0;authenticated=accepting=false;}
#endif
   break;
  case EventType::LinkError:
#ifdef UNIFIED_DIAGNOSTICS
   Serial.printf("UF_BLE link failed status=%u\n",unsigned(event.handle));
#endif
   break;
  }
 }
 // Let hosts begin bond encryption during discovery, as the factory demo
 // does. A delayed request covers hosts that wait for the peripheral.
 if(securityStarted&&int32_t(millis()-securityStarted)>700){
  securityStarted=0;
  int result=ble_gap_security_initiate(connection);
#ifdef UNIFIED_DIAGNOSTICS
  Serial.printf("UF_BLE security fallback=%d\n",result);
#endif
  if(result!=0&&result!=BLE_HS_EALREADY)disconnect(connection);
 }
 if(radio&&!enabled&&connection!=noConnection){
  ble_gap_conn_desc desc{};
  if(ble_gap_conn_find(connection,&desc)||int32_t(millis()-stopStarted)>800)connection=noConnection;
 }
 if(radio&&connection==noConnection){
  if(!enabled)shutdownRadio();
  else if(!ble_gap_adv_active()){
   enabled=startAdvertising();
#ifdef UNIFIED_DIAGNOSTICS
   Serial.printf("UF_BLE advertising restarted=%d\n",enabled);
#endif
  }
 }
 return authenticated;
}

bool sendUnifiedHid(bool usb,bool mouse,const keyboardMap::Map &map,bool alternate,bool release,bool optionTap){
 if(usb&&!tud_hid_ready())return false;
 auto &keyboard=M5Cardputer.Keyboard;
 const auto &state=keyboard.keysState();
 if(mouse||release){
  uint8_t buttons=!release?unsigned(state.enter)|(unsigned(keyboard.isKeyPressed('\\'))<<1):0;
  int8_t x=release?0:int(keyboard.isKeyPressed('/'))-int(keyboard.isKeyPressed(','));
  int8_t y=release?0:int(keyboard.isKeyPressed('.'))-int(keyboard.isKeyPressed(';'));
  if(buttons!=mouseButtons||x||y||release){
   if(usb){usbMouse.buttons(buttons);if(x||y)usbMouse.move(x,y);}
   else{uint8_t bytes[]{buttons,uint8_t(x),uint8_t(y),0};notify(mouseReport,mouseSubscriber,bytes,sizeof(bytes));}
   mouseButtons=buttons;
  }
 }
 if(!mouse||release){
  auto bytes=release?std::array<uint8_t,8>{}:keyboardMap::report(map,alternate,state.fn,optionTap,
      [&](int key){return keyboard.keyDown(key/14,key%14);});
  if(usb){
   KeyReport report{};report.modifiers=bytes[0];
   memcpy(report.keys,bytes.data()+2,sizeof(report.keys));
   usbKeyboard.sendReport(&report);
  }else notify(keyboardReport,keyboardSubscriber,bytes.data(),bytes.size());
 }
 return true;
}
#endif
