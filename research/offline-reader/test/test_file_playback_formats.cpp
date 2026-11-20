#include "file_playback_formats.h"

using filePlayback::Format;
using filePlayback::format;
using filePlayback::needsOptionalModule;

static_assert(format("/Music/song.mp3") == Format::Mp3);
static_assert(format("/Music/song.ogg") == Format::Vorbis);
static_assert(format("/Music/song.aug") == Format::Vorbis);
static_assert(format("/Recordings/voice.wav") == Format::Wav);
static_assert(format("/CardputerPackages/Controls.txt") == Format::Unknown);
static_assert(format("/folder.ogg/no-extension") == Format::Unknown);
static_assert(format("/music/track.mp3.bak") == Format::Unknown);

static_assert(needsOptionalModule(Format::Mp3, false));
static_assert(needsOptionalModule(Format::Vorbis, false));
static_assert(!needsOptionalModule(Format::Mp3, true));
static_assert(!needsOptionalModule(Format::Vorbis, true));
static_assert(!needsOptionalModule(Format::Wav, false));

int main() {}
