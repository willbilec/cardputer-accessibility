#pragma once

#include <cstddef>
#include <deque>
#include <string_view>
#include <utility>

namespace playpalace {

inline size_t speechPartEnd(std::string_view text, size_t offset) {
    if (offset >= text.size()) return text.size();
    constexpr size_t maximum = 720;
    const auto window = text.substr(offset, maximum);
    const auto newline = window.find('\n');
    if (newline != window.npos) return offset + newline + 1;
    size_t end = offset + window.size();
    if (end == text.size()) return end;
    const auto space = window.rfind(' ');
    if (space != window.npos && space >= maximum / 2) return offset + space + 1;
    while (end > offset && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    return end == offset ? offset + window.size() : end;
}

template <class SpeechText>
class PendingSpeech {
public:
    void push(SpeechText text) {
        const size_t length = text->length();
        while (!empty() && (entries.size() >= 24 || bytes + length > 4096)) pop();
        entries.push_back(std::move(text));
        bytes += length;
    }

    bool empty() const { return entries.empty(); }

    SpeechText pop() {
        auto text = std::move(entries.front());
        entries.pop_front();
        bytes -= text->length();
        return text;
    }

    void clear() {
        entries.clear();
        bytes = 0;
    }

private:
    std::deque<SpeechText> entries;
    size_t bytes = 0;
};

}
