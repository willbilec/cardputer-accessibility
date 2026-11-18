#include <cassert>
#include "../src/speech_service.cpp"

int main() {
    assert(!speechSay("before init"));
    assert(speechServiceBegin() && fakeTask);
    assert(!speechRepeat());
    assert(speechSay("first"));
    assert(speechSay("second"));
    fakeTask(nullptr);
    assert(fakeSynthLast == "second" && M5Cardputer.Speaker.calls == 2);
    assert(M5Cardputer.Speaker.lastChannel == SPEECH_OUTPUT_CHANNEL);
    assert(Serial.output.find("second") == std::string::npos);
    assert(speechSay("cancel me"));
    M5Cardputer.Speaker.onPlay = speechStop;
    int before = M5Cardputer.Speaker.calls;
    fakeTask(nullptr);
    assert(M5Cardputer.Speaker.calls == before + 1);
    assert(speechRepeat());
    fakeTask(nullptr);
    assert(fakeSynthLast == "cancel me" && M5Cardputer.Speaker.calls == before + 3);
    assert(!speechSayAndWait("timeout", 20));
    speechStop();
    fakeOnDelay = []() { fakeOnDelay = nullptr; fakeTask(nullptr); };
    assert(speechSayAndWait("finished", 100));
    assert(fakeSynthLast == "finished");
    fakeSynthBegin = false;
    fakeTask(nullptr);
    assert(!speechSay("after failure"));
}
