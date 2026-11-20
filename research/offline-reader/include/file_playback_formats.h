#pragma once

#include <string_view>

namespace filePlayback {

enum class Format { Unknown, Wav, Mp3, Vorbis };

constexpr Format format(std::string_view lowerName) {
    const auto extension = lowerName.find_last_of('.');
    if (extension == std::string_view::npos) return Format::Unknown;
    const auto suffix = lowerName.substr(extension);
    if (suffix == ".wav") return Format::Wav;
    if (suffix == ".mp3") return Format::Mp3;
    if (suffix == ".ogg" || suffix == ".aug") return Format::Vorbis;
    return Format::Unknown;
}

constexpr bool needsOptionalModule(Format value, bool enabled) {
    return !enabled && (value == Format::Mp3 || value == Format::Vorbis);
}

}
