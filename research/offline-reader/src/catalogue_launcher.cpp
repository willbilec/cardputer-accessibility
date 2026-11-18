#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <mbedtls/sha256.h>
#include "headless_board.h"
#include "speech_service.h"
#include "firmware_layout.h"
#include "program_catalogue_generated.h"
#include "catalogue_ui.h"
#include "image_transfer.h"

static catalogueUi::Controller ui(kProgramCount);
static void announce(const String &text) { speechSay(text.c_str()); }
static String label(size_t index) { return index==kProgramCount?"Help":programs[index].label; }
static void focus() { announce(label(ui.selection)+", "+String(ui.selection+1)+" of "+String(kProgramCount+1)); }

static bool checkHash(const ProgramRecord &program, File *file=nullptr) {
 if(!readerLayoutValid(program.size)) return false;
 if(file && (file->size()!=program.size || !file->seek(0))) return false;
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
 bool ok=mbedtls_sha256_starts(&hash,0)==0;
 uint8_t buffer[1024],digest[32];size_t offset=0;
 while(ok && offset<program.size) {
  size_t n=std::min(sizeof(buffer),size_t(program.size-offset));
  if(file) { int read=file->read(buffer,n);if(read<=0 || size_t(read)>n){ok=false;break;}n=read; }
  else if(esp_partition_read(readerPlayer(),offset,buffer,n)!=ESP_OK){ok=false;break;}
  ok=mbedtls_sha256_update(&hash,buffer,n)==0;offset+=n;delay(1);
 }
 if(ok) ok=mbedtls_sha256_finish(&hash,digest)==0 && !memcmp(digest,program.sha,32);
 mbedtls_sha256_free(&hash);return ok && offset==program.size;
}
static void run(const ProgramRecord &program) {
 if(!checkHash(program)) { announce("Installed copy did not match. Choose the program again to install it from SD");return; }
 if(esp_ota_set_boot_partition(readerPlayer())!=ESP_OK) { announce("Program is not bootable. Staying in launcher");return; }
 String message="Starting ";message+=program.label;
 speechSayAndWait(message.c_str(),5000);ESP.restart();
}
static void install(const ProgramRecord &program) {
 const auto *running=esp_ota_get_running_partition();
 if(!readerLayoutValid(program.size) || !running || running->address!=readerFactory()->address) {
  announce("Different flash layout. Installation refused. No partitions changed");return;
 }
 SPI.begin(40,39,14,12);
 if(!SD.begin(12,SPI,25000000)) { announce("SD card unavailable. Insert card and try again");return; }
 File file=SD.open(program.path,FILE_READ);
 announce("Checking SD image");
 if(!file || !checkHash(program,&file)) { announce("Missing or different program image. Installation refused");return; }
 if(!file.seek(0)) { announce("SD read failed. Installation refused");return; }
 esp_ota_handle_t handle;
 if(esp_ota_begin(readerPlayer(),program.size,&handle)!=ESP_OK){announce("Could not start installation");return;}
 int lastMilestone=-1;
 bool ok=transferImage<1024>(program.size,
  [&](uint8_t *buffer,size_t n){return file.read(buffer,n);},
  [&](const uint8_t *buffer,size_t n){return esp_ota_write(handle,buffer,n)==ESP_OK;},
  [&](size_t done,size_t total){int milestone=(done*4/total)*25;if(milestone!=lastMilestone){
   lastMilestone=milestone;announce("Installing. "+String(milestone)+" percent");}delay(1);});
 file.close();
 if(!ok){esp_ota_abort(handle);announce("Installation failed. Launcher preserved. Retry from SD");return;}
 if(esp_ota_end(handle)!=ESP_OK || !checkHash(program)){announce("Verification failed. Launcher preserved. Retry from SD");return;}
 run(program);
}
static void select(size_t index) {
 if(index>=kProgramCount) return;
 announce("Checking "+String(programs[index].label));
 if(checkHash(programs[index])) {run(programs[index]);return;}
 ui.askInstall(index);
 announce("Install "+String(programs[index].label)+" from SD? Replaces the current application. Enter confirms. Other keys cancel");
}
void setup() {
 Serial.begin(115200);M5Cardputer.begin();M5Cardputer.Speaker.setVolume(128);speechServiceBegin();
 announce("Accessible programs. Semicolon and period move. Enter runs or offers installation. R repeats; Space stops speech. Media player");
}
void loop() {
 M5Cardputer.update();auto &keyboard=M5Cardputer.Keyboard;
 bool pressed=false;for(auto event:keyboard.events()) pressed|=event.down;
 if(!pressed){delay(5);return;}
 if(keyboard.keysState().space){speechStop();return;}
 if(keyboard.isKeyPressed('r')){speechRepeat();return;}
 auto key=keyboard.keysState().enter?catalogueUi::Key::Enter:keyboard.isKeyPressed(';')?catalogueUi::Key::Up:
  keyboard.isKeyPressed('.')?catalogueUi::Key::Down:catalogueUi::Key::Other;
 auto reply=ui.handle(key);
 switch(reply.action){
  case catalogueUi::Action::Focus:focus();break;
  case catalogueUi::Action::Select:select(reply.index);break;
  case catalogueUi::Action::Install:install(programs[reply.index]);break;
  case catalogueUi::Action::Cancel:announce("Installation cancelled");break;
  case catalogueUi::Action::Help:announce("All programs stay on SD. One application is installed in flash at a time. Choosing another replaces only that application. Launcher is preserved");break;
  default:break;
 }
}
