#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include "settings_codec.h"

// Small settings live on the removable card, mirrored in a separate NVS
// namespace. Values are hex-encoded for safe parsing, not encrypted.
class SettingsStore {
public:
 explicit SettingsStore(fs::FS &storage):SD(storage){}
 bool begin(bool sdAvailable){
  bool ok=flash.begin("unified",false);attachSD(sdAvailable);return ok;
 }
 void attachSD(bool available){
  sdWritable=false;if(!available)return;
  if(!SD.exists(dir)&&!SD.mkdir(dir))return;
  bool mainExists=SD.exists(mainPath),backupExists=SD.exists(backupPath);
  settingsCodec::Values loaded;
  if(mainExists&&read(mainPath,loaded)){values.swap(loaded);sdWritable=true;return;}
  if(backupExists&&read(backupPath,loaded)){values.swap(loaded);sdWritable=!mainExists;return;}
  // Never replace an unfamiliar/corrupt file under this name.
  if(!mainExists&&!backupExists)sdWritable=true;
 }
 int getInt(const char *key,int fallback){auto it=values.find(key);if(it!=values.end())return String(it->second.c_str()).toInt();int value=flash.getInt(key,fallback);values[key]=std::to_string(value);return value;}
 bool getBool(const char *key,bool fallback){auto it=values.find(key);if(it!=values.end())return it->second=="1";bool value=flash.getBool(key,fallback);values[key]=value?"1":"0";return value;}
 String getString(const char *key,const char *fallback=""){auto it=values.find(key);if(it!=values.end())return String(it->second.c_str());String value=flash.getString(key,fallback);values[key]=value.c_str();return value;}
 bool putInt(const char *key,int value,bool deferCard=false){
  flash.putInt(key,value);values[key]=std::to_string(value);
  if(deferCard){cardPending=true;return true;}return save();
 }
 bool putVolumes(int reader,int media,bool deferCard=false){
  flash.putInt("volume",reader);values["volume"]=std::to_string(reader);
  flash.putInt("mediaVol",media);values["mediaVol"]=std::to_string(media);
  if(deferCard){cardPending=true;return true;}return save();
 }
 void flushDeferred(){if(cardPending&&save())cardPending=false;}
 bool putBool(const char *key,bool value){flash.putBool(key,value);values[key]=value?"1":"0";return save();}
 bool putString(const char *key,const String &value){flash.putString(key,value);values[key]=value.c_str();return save();}
 bool putWifi(const String &ssid,const String &password){
  values["wifiSsid"]=ssid.c_str();
  values["wifiPass"]=password.c_str();
  values["wifiOn"]="1";
  flash.putString("wifiSsid",ssid);
  flash.putString("wifiPass",password);
  flash.putBool("wifiOn",true);
  const bool mirrored=flash.getString("wifiSsid")==ssid&&
                      flash.getString("wifiPass")==password&&flash.getBool("wifiOn");
  const bool stored=save();
  return stored||mirrored;
 }
 bool availableOnSD() const{return sdWritable;}
private:
 fs::FS &SD;
 Preferences flash;settingsCodec::Values values;bool sdWritable=false,cardPending=false;
 const char *dir="/cardputer-access";
 const char *mainPath="/cardputer-access/settings.dat";
 const char *backupPath="/cardputer-access/settings.bak";
 const char *tempPath="/cardputer-access/settings.tmp";
 bool read(const char *path,settingsCodec::Values &out){
  File file=SD.open(path,FILE_READ);if(!file||file.size()>32768)return false;
  std::string data;data.reserve(file.size());while(file.available()){int c=file.read();if(c<0)return false;data+=char(c);}file.close();return settingsCodec::decode(data,out);
 }
 bool save(){
  if(!sdWritable)return false;
  settingsCodec::Values check;
  if(SD.exists(tempPath)){if(!read(tempPath,check))return false;if(!SD.remove(tempPath))return false;}
  std::string data=settingsCodec::encode(values);File file=SD.open(tempPath,FILE_WRITE);if(!file)return false;
  bool ok=file.write((const uint8_t*)data.data(),data.size())==data.size();file.flush();file.close();
  if(!ok||!read(tempPath,check)){SD.remove(tempPath);return false;}
  bool hadMain=SD.exists(mainPath);
  if(SD.exists(backupPath)){if(!read(backupPath,check)||!SD.remove(backupPath))return false;}
  if(hadMain&&!SD.rename(mainPath,backupPath))return false;
  if(!SD.rename(tempPath,mainPath)){if(hadMain)SD.rename(backupPath,mainPath);return false;}
  return true;
 }
};
