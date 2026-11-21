#include "playpalace_speech.h"

#include <cassert>
#include <memory>
#include <string>

int main() {
    using playpalace::speechPartEnd;
    std::string speech = "Turn one\n\n" + std::string(717, 'a') + " \xF0\x9F\x9A\x80" +
        std::string(750, 'b') + "\nFinal score: 31.";
    assert(speechPartEnd(speech, 0) == 9);
    assert(speechPartEnd(speech, 9) == 10);
    assert(speechPartEnd(speech, 10) == 728);
    std::string joined;
    for (size_t offset = 0; offset < speech.size();) {
        const size_t end = speechPartEnd(speech, offset);
        assert(end > offset && end - offset <= 720);
        if (end < speech.size())
            assert((static_cast<unsigned char>(speech[end]) & 0xc0) != 0x80);
        joined.append(speech, offset, end - offset);
        offset = end;
    }
    assert(joined == speech);
    const std::string unicode = std::string(719, 'x') + "\xC3\xA9";
    assert(speechPartEnd(unicode, 0) == 719);

    using Text = std::shared_ptr<const std::string>;
    playpalace::PendingSpeech<Text> pending;
    for (int turn = 0; turn < 12; ++turn) pending.push(std::make_shared<std::string>(std::to_string(turn)));
    auto reading = pending.pop();
    for (int turn = 1; turn < 12; ++turn) assert(*pending.pop() == std::to_string(turn));
    assert(*reading == "0" && pending.empty());

    for (int turn = 0; turn < 25; ++turn) pending.push(std::make_shared<std::string>(std::to_string(turn)));
    assert(*pending.pop() == "1");
    pending.push(std::make_shared<std::string>(5000, 'x'));
    assert(pending.pop()->size() == 5000 && pending.empty());
    pending.push(std::make_shared<std::string>(3000, 'x'));
    pending.push(std::make_shared<std::string>(1100, 'y'));
    assert(pending.pop()->front() == 'y' && pending.empty());
    pending.push(std::make_shared<std::string>(3000, 'x'));
    pending.clear();
    pending.push(std::make_shared<std::string>(3000, 'x'));
    pending.push(std::make_shared<std::string>(1000, 'y'));
    assert(pending.pop()->front() == 'x');
    assert(pending.pop()->front() == 'y' && pending.empty());
}
