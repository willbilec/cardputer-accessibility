#pragma once

#ifdef LAUNCHER_ACCESS_SPEECH
bool accessSpeechBegin();
void accessSpeak(const char *text);
void accessSpeakAndWait(const char *text, unsigned timeoutMs);
#endif
