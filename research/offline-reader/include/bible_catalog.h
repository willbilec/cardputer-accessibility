#pragma once

#include <Arduino.h>
#include <FS.h>
#include <algorithm>
#include <vector>

namespace bible {
inline constexpr const char* root = "/bible";
inline constexpr const char* names[] = {
 "Genesis","Exodus","Leviticus","Numbers","Deuteronomy","Joshua","Judges","Ruth",
 "1 Samuel","2 Samuel","1 Kings","2 Kings","1 Chronicles","2 Chronicles","Ezra",
 "Nehemiah","Esther","Job","Psalms","Proverbs","Ecclesiastes","Song of Songs",
 "Isaiah","Jeremiah","Lamentations","Ezekiel","Daniel","Hosea","Joel","Amos",
 "Obadiah","Jonah","Micah","Nahum","Habakkuk","Zephaniah","Haggai","Zechariah",
 "Malachi","Matthew","Mark","Luke","John","Acts","Romans","1 Corinthians",
 "2 Corinthians","Galatians","Ephesians","Philippians","Colossians",
 "1 Thessalonians","2 Thessalonians","1 Timothy","2 Timothy","Titus","Philemon",
 "Hebrews","James","1 Peter","2 Peter","1 John","2 John","3 John","Jude","Revelation"
};
inline constexpr int bookCount = sizeof(names)/sizeof(*names);
struct Chapter { int number; String path; };

inline String basename(const File& file) {
 const String name = file.name();
 return name.substring(name.lastIndexOf('/')+1);
}

inline String basename(const String& path) {
 return path.substring(path.lastIndexOf('/')+1);
}

inline int bookNumber(const String& folder) {
 const int number = folder.toInt();
 if(number < 1 || number > bookCount || folder.indexOf('-') < 0) return 0;
 String label = folder.substring(folder.indexOf('-')+1);
 label.trim();
 if(number==21 && (label.equalsIgnoreCase("Song of Songs") ||
                  label.equalsIgnoreCase("Song of Solomon"))) return 22;
 return number;
}

inline std::vector<String> versions(fs::FS& sd) {
 std::vector<String> result;
 File dir = sd.open(root);
 if(!dir || !dir.isDirectory()) return result;
 for(File entry=dir.openNextFile(); entry; entry=dir.openNextFile()) {
  String name=basename(entry);
  if(!entry.isDirectory() || name.startsWith(".")) continue;
  for(File book=entry.openNextFile(); book; book=entry.openNextFile()) {
   if(book.isDirectory() && bookNumber(basename(book))) {
    result.push_back(name);
    break;
   }
  }
 }
 std::sort(result.begin(),result.end());
 return result;
}

inline std::vector<Chapter> chapters(fs::FS& sd,const String& version,int book) {
 std::vector<Chapter> result;
 const String base=String(root)+"/"+version;
 File dir=sd.open(base);
 if(!dir || !dir.isDirectory()) return result;
 bool isDirectory;
 for(String folderPath=dir.getNextFileName(&isDirectory); !folderPath.isEmpty();
     folderPath=dir.getNextFileName(&isDirectory)) {
  if(!isDirectory) continue;
  const String label=basename(folderPath);
  const int number=bookNumber(label);
  if(number!=book && !(book==64 && number==43)) continue;
  File folder=sd.open(folderPath);
  bool isFolder;
  for(String filePath=folder.getNextFileName(&isFolder); !filePath.isEmpty();
      filePath=folder.getNextFileName(&isFolder)) {
   if(isFolder) continue;
   const String name=basename(filePath);
   String lower=name;
   lower.toLowerCase();
   if(!lower.endsWith(".mp3")) continue;
   const int separator=name.lastIndexOf(' ');
   if(separator<0) continue;
   const String digits=name.substring(separator+1,name.length()-4);
   if(!digits.length() || digits.length()>3) continue;
   bool numeric=true;
   for(char digit:digits) if(digit<'0'||digit>'9') numeric=false;
   const int chapter=digits.toInt();
   if(!numeric || !chapter) continue;
   int prefix=name.lastIndexOf(" - ");
   const String title=name.substring(prefix<0?0:prefix+3,separator);
   bool matches=title.equalsIgnoreCase(names[book-1]);
   if(book==22 && title.equalsIgnoreCase("Song of Solomon")) matches=true;
   if(matches) result.push_back({chapter,std::move(filePath)});
  }
 }
 std::sort(result.begin(),result.end(),[](const Chapter& a,const Chapter& b){
  return a.number==b.number?a.path<b.path:a.number<b.number;
 });
 result.erase(std::unique(result.begin(),result.end(),[](const Chapter& a,const Chapter& b){
  return a.number==b.number;
 }),result.end());
 return result;
}
}
