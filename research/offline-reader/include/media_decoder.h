#pragma once

#include <FS.h>
#include <memory>

// Pull decoder for SD or other Arduino FS files. Call on an application task.
// The caller owns output, gain, scheduling and playback controls.
class MediaDecoder {
public:
    enum class Format { Mp3, Vorbis };
    enum class Result { Samples, End, Error };
    static constexpr size_t blockFrames = 128;
    // MP3 synthesis reserves headroom before converting to 16-bit PCM.
    static constexpr int mp3DecodeGainPercent = 75;
    struct Block {
        int16_t stereo[blockFrames * 2];
        size_t frames;
        unsigned rate;
        size_t mp3Offset;
        unsigned mp3BytesPerSecond;
    };

    MediaDecoder();
    ~MediaDecoder();
    // Reads from the current file position. File copies share the same cursor.
    // Vorbis support requires MEDIA_DECODER_VORBIS and micro-vorbis.
    bool open(File source, Format format);
    // Reposition within an open MP3 while keeping its decoder work buffers.
    bool seekMp3(size_t offset);
    using ReadSource = int (*)(void* context, uint8_t* buffer, size_t capacity);
    // The caller keeps the context alive. Zero signals EOF and negative signals an error.
    bool openVorbis(ReadSource read, void* context);
    Result read(Block& block);

private:
    struct State;
    std::unique_ptr<State> state;
};
