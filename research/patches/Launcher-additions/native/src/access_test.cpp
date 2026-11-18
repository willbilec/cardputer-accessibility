// Host-only speech sink for the native accessibility navigation test.
#ifdef NATIVE_ACCESS_TEST
#include <atomic>
#include <cstdio>
#include <cstring>
#include <chrono>
std::atomic<unsigned> nativeAccessEventCount{0};
std::atomic<unsigned> nativeAccessSeen{0};
std::atomic<bool> nativeAccessUiReady{false};
void nativeAccessMenuReady() { nativeAccessUiReady.store(true); }
bool accessSpeechBegin() { return true; }
void accessSpeak(const char *text) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::printf("ACCESS_EVENT %lld: %s\n", static_cast<long long>(ms), text ? text : "");
    std::fflush(stdout);
    ++nativeAccessEventCount;
    if (text && std::strstr(text, "Launch from")) nativeAccessSeen.fetch_or(1);
    if (text && std::strcmp(text, "Online Installer") == 0) nativeAccessSeen.fetch_or(2);
    if (text && std::strcmp(text, "Start Web User Interface") == 0) nativeAccessSeen.fetch_or(4);
}
void accessSpeakAndWait(const char *text, unsigned) { accessSpeak(text); }
#endif
