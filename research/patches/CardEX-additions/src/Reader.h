#pragma once
#include <Arduino.h>
// Semantic UI-task interface. Speech runs in BackgroundAudio's audio task.
namespace Reader {
#ifdef CARDEX_SPEECH
void begin();
void poll();
void event(const String &text);
void focus(const String &id,const String &text);
void context(const String &text);
void repeat();
void next();
void stop();
void progress(int percent,const String &text);
#else
inline void begin(){} inline void poll(){}
inline void event(const String&){} inline void focus(const String&,const String&){}
inline void context(const String&){} inline void repeat(){} inline void next(){}
inline void stop(){} inline void progress(int,const String&){}
#endif
}
