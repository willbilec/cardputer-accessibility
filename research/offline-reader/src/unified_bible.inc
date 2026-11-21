// Bible app UI and transport, using the unified MP3 decoder and mixer.
enum class BibleMenu { Player, Books, Chapters, Versions };
struct BiblePosition {
 String version;
 int book=1, chapter=0;
 size_t offset=0;
};
struct {
 std::vector<String> versions;
 std::vector<bible::Chapter> choices;
 String catalogVersion;
 int catalogBook=0;
 BiblePosition position;
 BibleMenu menu=BibleMenu::Player;
 int cursor=0, book=1;
 int speed=100;
 bool browseWasPlaying=false, chaptersFromBooks=false, finished=false, seeking=false;
 bool keyboardLocked=false;
 size_t seekOrigin=0;
 unsigned bytesPerSecond=0, scanned=0, quiet=0;
} bibleApp;

bool applyBibleSpeed(int speed){
 if(!bibleTempo && speed!=100){
  bibleTempo=sonicCreateStream(compressedRate,1);
  if(!bibleTempo)return false;
  sonicSetQuality(bibleTempo,1);
  bibleTempoFlushed=false;
 }
 if(bibleTempo&&speed!=100){
  sonicSetSpeed(bibleTempo,speed/100.0f);
  bibleTempoFlushed=false;
 }
 return true;
}

void adjustBibleSpeed(int adjustment){
 const int speed=constrain(bibleApp.speed+adjustment,50,135);
 if(speed!=bibleApp.speed){
  if(playing&&!applyBibleSpeed(speed)){
   say("Not enough memory to change playback speed");return;
  }
  if(!settings.putMirroredString("bibleSpeed",String(speed))){
   if(playing)applyBibleSpeed(bibleApp.speed);
   say("Could not save Bible playback speed");return;
  }
  bibleApp.speed=speed;
 }
 say("Playback speed "+String(speed)+" percent");
}

String bibleReference(int book,int chapter){
 return String(bible::names[book-1])+" chapter "+String(chapter);
}

BiblePosition readBiblePosition(const char* key){
 const String value=settings.getString(key);
 const int split=value.lastIndexOf('\n');
 if(split<0)return {};
 unsigned book=0,chapter=0,offset=0;
 if(sscanf(value.c_str()+split+1,"%u %u %u",&book,&chapter,&offset)!=3 ||
    !book || book>bible::bookCount || chapter>999)return {};
 return {value.substring(0,split),int(book),int(chapter),offset};
}

bool writeBiblePosition(const char* key,const BiblePosition& position){
 const String value=position.version+"\n"+String(position.book)+" "+String(position.chapter)+" "+String(position.offset);
 return settings.getString(key)==value || settings.putMirroredString(key,value);
}

BiblePosition currentBiblePosition(){
 auto position=bibleApp.position;
 if(bibleApp.seeking){position.offset=bibleApp.seekOrigin;return position;}
 if(playing && bibleApp.bytesPerSecond && compressedRate){
  size_t queued;
  {SpeechLock lock;queued=compressedQueueBytes-std::min(compressedQueueBytes,size_t(compressedLeg.availableForWrite()));}
  const unsigned speed=bibleTempo?bibleApp.speed:100;
  const size_t bufferedBytes=uint64_t(queued)*bibleApp.bytesPerSecond*speed/(compressedRate*400);
  position.offset=position.offset>bufferedBytes?position.offset-bufferedBytes:0;
 }
 return position;
}

bool closeBible(){
 const bool saved=bibleApp.position.version.isEmpty()||
                  writeBiblePosition("bibleResume",currentBiblePosition());
 bibleApp={};
 return saved;
}

const std::vector<bible::Chapter>& bibleChapters(const String& version,int book){
 if(bibleApp.catalogBook!=book || bibleApp.catalogVersion!=version){
  bibleApp.choices=bible::chapters(card,version,book);
  bibleApp.catalogVersion=version;
  bibleApp.catalogBook=book;
 }
 return bibleApp.choices;
}

bool playBible(BiblePosition position,bool announce=true,bool preservePause=false){
 const auto& chapters=bibleChapters(position.version,position.book);
 const auto chapter=std::find_if(chapters.begin(),chapters.end(),[&](const bible::Chapter& item){
  return item.number==position.chapter || position.chapter==0;
 });
 if(chapter==chapters.end()){
  say(position.chapter?bibleReference(position.book,position.chapter)+" is unavailable":
                       String("No chapters available for ")+bible::names[position.book-1]);
  return false;
 }
 const bool wasPaused=paused;
 if(!startPlayback(chapter->path,position.offset))return false;
 const bool speedApplied=applyBibleSpeed(bibleApp.speed);
 position.chapter=chapter->number;
 bibleApp.position=std::move(position);
 bibleApp.position.offset=compressedBlock.mp3Offset;
 bibleApp.bytesPerSecond=compressedBlock.mp3BytesPerSecond;
 bibleApp.menu=BibleMenu::Player;
 bibleApp.finished=bibleApp.seeking=false;
 paused=preservePause&&wasPaused;
 if(!speedApplied)say("Playing at normal speed. Not enough memory for the saved playback speed.");
 else if(announce)say(bibleReference(bibleApp.position.book,bibleApp.position.chapter));
 return true;
}

void openBible(){
 page=Page::Bible;
 bibleApp.speed=constrain(settings.getString("bibleSpeed","100").toInt(),50L,135L);
 bibleApp.versions=bible::versions(card);
 if(bibleApp.versions.empty()){
  say("No Bible versions found. Put version folders under Bible on the SD card. Alt Q returns home.");
  return;
 }
 const auto saved=readBiblePosition("bibleResume");
 bibleApp.position.version=bibleApp.versions.front();
 if(std::find(bibleApp.versions.begin(),bibleApp.versions.end(),saved.version)!=bibleApp.versions.end()){
  bibleApp.position.version=saved.version;
  if(saved.chapter && playBible(saved,false)){
   say("Bible. "+saved.version+". Resuming "+bibleReference(saved.book,saved.chapter)+". Option H for help.");
   return;
  }
  bibleApp.position.book=saved.book;
 }
 say("Bible. "+bibleApp.position.version+". B chooses a book. Space starts playing. Option H for help.");
}

bool stepBible(bool forward,bool announce=true){
 const auto position=bibleApp.position;
 if(position.version.isEmpty())return false;
 const int step=forward?1:-1;
 for(int book=position.book;book>=1&&book<=bible::bookCount;book+=step){
  const auto& chapters=bibleChapters(position.version,book);
  if(forward){
   for(const auto& chapter:chapters)
    if(book!=position.book || chapter.number>position.chapter)
     return playBible({position.version,book,chapter.number,0},announce,playing);
  }else{
   for(auto chapter=chapters.rbegin();chapter!=chapters.rend();++chapter)
    if(book!=position.book || chapter->number<position.chapter)
     return playBible({position.version,book,chapter->number,0},announce,playing);
  }
 }
 say(forward?"End of this Bible version":"Beginning of this Bible version");
 return false;
}

void finishBibleTrack(){
 const bool failed=compressedFailed;
 if(bibleApp.seeking){
  const auto origin=currentBiblePosition();
  bibleApp.seeking=false;
  if(playBible(origin,false))say("No nearby seek point");
  return;
 }
 stopMedia();
 bibleApp.finished=true;
 if(failed)say("Chapter playback stopped because the audio could not be decoded");
 else stepBible(true,false);
}

bool prepareBibleBlock(){
 if(bibleApp.seeking){
  int peak=0;
  for(size_t i=0;i<compressedBlock.frames*2;++i)
   peak=std::max(peak,abs(int(compressedBlock.stereo[i])));
  bibleApp.scanned+=compressedBlock.frames;
  bibleApp.quiet=peak<550?bibleApp.quiet+compressedBlock.frames:0;
  if(bibleApp.quiet<compressedRate/6 && bibleApp.scanned<compressedRate*2){
   compressedBlock.frames=0;
   return false;
  }
  bibleApp.seeking=false;
 }
 bibleApp.position.offset=compressedBlock.mp3Offset;
 bibleApp.bytesPerSecond=compressedBlock.mp3BytesPerSecond;
 return true;
}

void seekBible(bool forward){
 if(!playing || !bibleApp.bytesPerSecond)return;
 auto target=currentBiblePosition();
 const size_t checkpoint=target.offset;
 const size_t origin=bibleApp.seeking?bibleApp.position.offset:checkpoint;
 const size_t distance=bibleApp.bytesPerSecond*(forward?6:10);
 const size_t size=audio.size();
 target.offset=forward?std::min(origin+distance,size-1):origin>distance?origin-distance:0;
 if(forward && target.offset>=size-std::min(size,size_t(bibleApp.bytesPerSecond*2))){
  say("Near the end of the chapter. Up goes to the next chapter.");
  return;
 }
 if(!target.offset){
  playBible(target,false,true);
  return;
 }
 if(!compressedAudio->seekMp3(target.offset)){say("Could not seek in this chapter");return;}
 if(bibleTempo)cardputer_sonic_discard(bibleTempo);
 bibleTempoFlushed=false;
 {SpeechLock lock;compressedLeg.discard();}
 mediaBass=0;
 musicDynamics.reset(compressedRate,mediaQuietBoost);
 compressedBlock.frames=0;
 compressedRemainder=0;
 compressedEnded=compressedFailed=false;
 // A paused seek finds its quiet gap when playback resumes.
 bibleApp.position.offset=target.offset;
 bibleApp.seeking=true;
 bibleApp.seekOrigin=checkpoint;
 bibleApp.scanned=bibleApp.quiet=0;
}

int bibleMenuCount();
void focusBibleMenu(){
 switch(bibleApp.menu){
 case BibleMenu::Books:say(bible::names[bibleApp.cursor]);break;
 case BibleMenu::Chapters:say(bibleReference(bibleApp.book,bibleApp.choices[bibleApp.cursor].number));break;
 case BibleMenu::Versions:say(bibleApp.versions[bibleApp.cursor]);break;
 default:break;
 }
#ifdef UNIFIED_SCREEN
 const char *title=bibleApp.menu==BibleMenu::Books?"Bible books":
   bibleApp.menu==BibleMenu::Chapters?"Bible chapters":"Bible versions";
 unifiedScreen::menu(title,unifiedScreen::Icon::Book,bibleMenuCount(),bibleApp.cursor,
   [](int index)->String{
    if(bibleApp.menu==BibleMenu::Books)return bible::names[index];
    if(bibleApp.menu==BibleMenu::Chapters)return "Chapter "+String(bibleApp.choices[index].number);
    return bibleApp.versions[index];
   });
#endif
}

int bibleMenuCount(){
 switch(bibleApp.menu){
 case BibleMenu::Books:return bible::bookCount;
 case BibleMenu::Chapters:return bibleApp.choices.size();
 case BibleMenu::Versions:return bibleApp.versions.size();
 default:return 0;
 }
}

void moveBibleMenu(char key){
 const int count=bibleMenuCount();
 if(!count)return;
 if(key==','||key=='/')bibleApp.cursor=key==','?0:count-1;
 else bibleApp.cursor=(bibleApp.cursor+count+(key==';'?-1:1))%count;
 focusBibleMenu();
}

void jumpBibleMenu(char key){
 const bool digit=key>='0'&&key<='9';
 if(bibleApp.menu==BibleMenu::Books&&(digit||(key>='a'&&key<='z'))){
  const auto matches=[key](int index){return tolower(bible::names[index][0])==key;};
  const int start=matches(bibleApp.cursor)?bibleApp.cursor+1:0;
  for(int i=0;i<bible::bookCount;++i){
   const int index=(start+i)%bible::bookCount;
   if(matches(index)){bibleApp.cursor=index;focusBibleMenu();return;}
  }
  say("No book starts with "+String(key));
 }else if(bibleApp.menu==BibleMenu::Chapters&&digit){
  const int number=key-'0';
  const auto& chapters=bibleApp.choices;
  auto match=std::find_if(chapters.begin(),chapters.end(),[number](const bible::Chapter& chapter){
   return chapter.number>=number*10&&chapter.number<number*10+10;
  });
  if(number==0||match==chapters.end())
   match=std::find_if(chapters.begin(),chapters.end(),[number](const bible::Chapter& chapter){return chapter.number==number;});
  if(match==chapters.end()){say("No chapter for "+String(key));return;}
  bibleApp.cursor=match-chapters.begin();
  focusBibleMenu();
 }
}

void browseBible(BibleMenu menu){
 if(bibleApp.menu==BibleMenu::Player){
  bibleApp.browseWasPlaying=playing&&!paused;
  if(playing)paused=true;
 }
 bibleApp.menu=menu;
}

void closeBibleMenu(){
 bibleApp.menu=BibleMenu::Player;
 if(playing)paused=!bibleApp.browseWasPlaying;
 const auto& position=bibleApp.position;
 say(position.chapter?bibleReference(position.book,position.chapter):position.version);
}

void browseBibleChapters(bool fromBooks){
 const auto& position=bibleApp.position;
 const int book=fromBooks?bibleApp.cursor+1:position.book;
 const auto& chapters=bibleChapters(position.version,book);
 if(chapters.empty()){say("No chapters available");return;}
 bibleApp.book=book;
 bibleApp.chaptersFromBooks=fromBooks;
 browseBible(BibleMenu::Chapters);
 const auto current=std::find_if(chapters.begin(),chapters.end(),[&](const bible::Chapter& chapter){
  return !fromBooks&&chapter.number==position.chapter;
 });
 bibleApp.cursor=current==chapters.end()?0:current-chapters.begin();
 focusBibleMenu();
}

void handleBible(const HeadlessKeyboard::Event& event,const HeadlessKeyboard::State& state){
 const char key=keys::plain[event.row][event.col];
 const bool enter=event.row==2&&event.col==13;
 const bool back=event.row==0&&event.col==13;
 const bool escape=state.fn&&event.row==0&&event.col==0;
 if(state.ctrl||state.alt||state.shift)return;
 if(state.opt){
  if(!state.fn&&key=='m'&&bibleApp.menu==BibleMenu::Player){
   const auto bookmark=readBiblePosition("bibleBookmark");
   if(!bookmark.chapter)say("No Bible bookmark saved");
   else playBible(bookmark);
  }
  return;
 }
 if(!state.fn&&(key=='-'||key=='=')){
  adjustMediaVolume(key=='-'?-5:5);
  return;
 }
 if(back||escape){
  if(bibleApp.menu==BibleMenu::Chapters&&bibleApp.chaptersFromBooks){
   bibleApp.menu=BibleMenu::Books;bibleApp.cursor=bibleApp.book-1;focusBibleMenu();
  }else if(bibleApp.menu!=BibleMenu::Player)closeBibleMenu();
  else homePage();
  return;
 }
 auto& position=bibleApp.position;
 if(position.version.isEmpty())return;
 if(bibleApp.menu!=BibleMenu::Player){
  if(key==';'||key=='.'||key==','||key=='/'){
   moveBibleMenu(key);
  }else if(!state.fn&&key==' ')focusBibleMenu();
  else if(!state.fn&&enter){
   if(bibleApp.menu==BibleMenu::Books){
    browseBibleChapters(true);
   }else if(bibleApp.menu==BibleMenu::Chapters){
    playBible({position.version,bibleApp.book,bibleApp.choices[bibleApp.cursor].number,0});
   }else{
    const String version=bibleApp.versions[bibleApp.cursor];
    if(version==position.version){closeBibleMenu();return;}
    const auto old=currentBiblePosition();
    stopMedia();
    bibleApp.position={version,old.book,0,0};
    bibleApp.seeking=bibleApp.finished=false;
    bibleApp.menu=BibleMenu::Player;
    if(old.chapter && playBible({version,old.book,old.chapter,0},false))
     say(version+". "+bibleReference(old.book,old.chapter));
    else say(version+". "+bible::names[old.book-1]+". B chooses a book.");
    writeBiblePosition("bibleResume",bibleApp.position);
   }
  }else if(!state.fn)jumpBibleMenu(key);
  return;
 }
 if(key==';'||key=='.'){stepBible(key==';');return;}
 if(key==','||key=='/'){seekBible(key=='/');return;}
 if(state.fn)return;
 if(key=='b'){
  browseBible(BibleMenu::Books);bibleApp.cursor=position.book-1;focusBibleMenu();
 }else if(key=='c'){
  browseBibleChapters(false);
 }else if(key=='v'){
  if(bibleApp.versions.size()==1){say(position.version+". Only version available.");return;}
  browseBible(BibleMenu::Versions);
  bibleApp.cursor=std::find(bibleApp.versions.begin(),bibleApp.versions.end(),position.version)-bibleApp.versions.begin();
  focusBibleMenu();
 }else if(key=='m'){
  const auto bookmark=currentBiblePosition();
  if(!bookmark.chapter)say("Start a chapter before saving a bookmark");
  else if(writeBiblePosition("bibleBookmark",bookmark))say("Bookmark saved. "+bibleReference(bookmark.book,bookmark.chapter));
  else say("Could not save bookmark");
 }else if(key=='['||key==']'){
  adjustBibleSpeed(key=='['?-5:5);
 }else if(key==' '||enter){
  if(playing)togglePlaybackPause();
  else playBible({position.version,position.book,position.chapter,bibleApp.finished?0:position.offset});
 }
}
