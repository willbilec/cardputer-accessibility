#include "media_decoder.h"

#include <algorithm>
#include <cstring>
#include <new>
#if !defined(UNIFIED_CUSTOM_BUILD) || defined(MEDIA_DECODER_MP3)
#define CARDPUTER_MP3_DECODER
#include <libmad/config.h>
#include <libmad/mad.h>
#endif
#ifdef MEDIA_DECODER_VORBIS
#include <micro_vorbis/ogg_vorbis_decoder.h>
#endif

// Keep only one decoder allocated at a time.
struct MediaDecoder::State {
#ifdef CARDPUTER_MP3_DECODER
    struct Mp3 {
        static constexpr size_t inputCapacity = 4096;
        std::unique_ptr<mad_stream> stream;
        std::unique_ptr<mad_frame> frame;
        std::unique_ptr<mad_synth> synth;
        std::unique_ptr<uint8_t[]> input;
        size_t consumed = 0;
        size_t inputOffset = 0;
        bool eof = false;

        bool begin() {
            // Other services can fragment the heap between tracks. Allocate
            // libmad's independent work areas separately.
            stream.reset(new(std::nothrow) mad_stream);
            if (stream) mad_stream_init(stream.get());
            frame.reset(new(std::nothrow) mad_frame);
            if (frame) mad_frame_init(frame.get());
            synth.reset(new(std::nothrow) mad_synth);
            if (synth) mad_synth_init(synth.get());
            input.reset(new(std::nothrow) uint8_t[inputCapacity + MAD_BUFFER_GUARD]);
            return stream && frame && synth && input;
        }
        ~Mp3() {
            if (frame) mad_frame_finish(frame.get());
            if (stream) mad_stream_finish(stream.get());
        }
    };

    std::unique_ptr<Mp3> mp3;
#endif
    File file;
    bool readFailed = false;
#ifdef MEDIA_DECODER_VORBIS
    struct Vorbis {
        micro_vorbis::OggVorbisDecoder decoder{2, true};
        ReadSource read = nullptr;
        void* context = nullptr;
        uint8_t input[2048];
        size_t inputOffset = 0, inputSize = 0;
        std::unique_ptr<int16_t[]> pcm;
        size_t pcmCapacity = 0, pcmOffset = 0, pcmSize = 0;
        bool eof = false, ended = false;
    };
    std::unique_ptr<Vorbis> vorbis;
#endif

    int readFile(void* target, size_t size) {
        const int count = file.read(static_cast<uint8_t*>(target), size);
        if (count < 0 || (!count && file.position() < file.size())) readFailed = true;
        return count > 0 ? count : 0;
    }
};

MediaDecoder::MediaDecoder() = default;
MediaDecoder::~MediaDecoder() = default;

bool MediaDecoder::open(File source, Format format) {
    state.reset(new(std::nothrow) State);
    if (!state || !source) return false;
    state->file = source;
    if (format == Format::Mp3) {
#ifdef CARDPUTER_MP3_DECODER
        // Skip leading ID3v2 metadata, which can contain large embedded images.
        const size_t start = source.position();
        if (!start) {
            uint8_t tag[10];
            if (state->readFile(tag, sizeof(tag)) == sizeof(tag) && !memcmp(tag, "ID3", 3)) {
                if ((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80) return false;
                const size_t size = (size_t(tag[6]) << 21) | (size_t(tag[7]) << 14) |
                                    (size_t(tag[8]) << 7) | tag[9];
                const size_t end = sizeof(tag) + size + ((tag[3] == 4 && (tag[5] & 0x10)) ? 10 : 0);
                if (end >= source.size() || !source.seek(end)) return false;
            } else if (state->readFailed || !source.seek(start)) return false;
        }
        state->mp3.reset(new(std::nothrow) State::Mp3);
        if (state->mp3 && state->mp3->begin()) return true;
        state.reset();
#endif
        return false;
    }
#ifdef MEDIA_DECODER_VORBIS
    const auto read = [](void* context, uint8_t* target, size_t size) {
        auto& source = *static_cast<State*>(context);
        const int count = source.readFile(target, size);
        return source.readFailed ? -1 : count;
    };
    state->vorbis.reset(new(std::nothrow) State::Vorbis);
    if (!state->vorbis) return false;
    state->vorbis->read = read;
    state->vorbis->context = state.get();
    return true;
#else
    return false;
#endif
}

bool MediaDecoder::openVorbis(ReadSource read, void* context) {
#ifdef MEDIA_DECODER_VORBIS
    state.reset(new(std::nothrow) State);
    if (!state || !read) return false;
    state->vorbis.reset(new(std::nothrow) State::Vorbis);
    if (!state->vorbis) return false;
    state->vorbis->read = read;
    state->vorbis->context = context;
    return true;
#else
    return false;
#endif
}

bool MediaDecoder::seekMp3(size_t offset) {
#ifdef CARDPUTER_MP3_DECODER
    if (!state || !state->mp3 || !offset || offset >= state->file.size() ||
        !state->file.seek(offset)) return false;
    auto& mp3 = *state->mp3;
    mad_frame_finish(mp3.frame.get());
    mad_stream_finish(mp3.stream.get());
    mad_stream_init(mp3.stream.get());
    mad_frame_init(mp3.frame.get());
    mad_synth_init(mp3.synth.get());
    mp3.consumed = 0;
    mp3.eof = state->readFailed = false;
    return true;
#else
    return false;
#endif
}

MediaDecoder::Result MediaDecoder::read(Block& block) {
    block.frames = 0;
    if (!state) return Result::Error;
#ifdef CARDPUTER_MP3_DECODER
    if (!state->mp3)
#endif
    {
#ifdef MEDIA_DECODER_VORBIS
        if (!state->vorbis) return Result::Error;
        auto& vorbis = *state->vorbis;
        using namespace micro_vorbis;
        if (!vorbis.pcm) {
            vorbis.pcm.reset(new(std::nothrow) int16_t[blockFrames * 2]);
            if (!vorbis.pcm) return Result::Error;
            vorbis.pcmCapacity = sizeof(block.stereo);
        }
        while (vorbis.pcmOffset == vorbis.pcmSize) {
            if (vorbis.ended) return Result::End;
            if (vorbis.inputOffset == vorbis.inputSize && !vorbis.eof) {
                const int count = vorbis.read(vorbis.context, vorbis.input, sizeof(vorbis.input));
                if (count < 0 || size_t(count) > sizeof(vorbis.input)) return Result::Error;
                vorbis.inputOffset = 0;
                vorbis.inputSize = count;
                vorbis.eof = !count;
            }
            size_t consumed = 0, written = 0;
            const auto result = vorbis.decoder.decode(
                vorbis.input + vorbis.inputOffset, vorbis.inputSize - vorbis.inputOffset,
                reinterpret_cast<uint8_t*>(vorbis.pcm.get()), vorbis.pcmCapacity, consumed, written);
            vorbis.inputOffset += consumed;
            if (result == OGG_VORBIS_DECODER_ERROR_OUTPUT_BUFFER_TOO_SMALL) {
                const size_t bytes = vorbis.decoder.get_required_output_bytes();
                if (!bytes || bytes <= vorbis.pcmCapacity || bytes > 16384 || bytes % 4)
                    return Result::Error;
                vorbis.pcm.reset();
                vorbis.pcm.reset(new(std::nothrow) int16_t[bytes / sizeof(int16_t)]);
                if (!vorbis.pcm) return Result::Error;
                vorbis.pcmCapacity = bytes;
                continue;
            }
            if (result < 0 || written % 4 || written > vorbis.pcmCapacity) return Result::Error;
            vorbis.ended = result == OGG_VORBIS_DECODER_END_OF_STREAM;
            vorbis.pcmOffset = 0;
            vorbis.pcmSize = written;
            if (!consumed && !written && !vorbis.ended) return Result::Error;
        }
        const auto& format = vorbis.decoder.get_pcm_format();
        block.rate = format.sample_rate();
        if (format.num_channels() != 2 || block.rate < 8000 || block.rate > 48000)
            return Result::Error;
        const size_t bytes = std::min(sizeof(block.stereo), vorbis.pcmSize - vorbis.pcmOffset);
        memcpy(block.stereo, vorbis.pcm.get() + vorbis.pcmOffset / sizeof(int16_t), bytes);
        vorbis.pcmOffset += bytes;
        block.frames = bytes / 4;
        return Result::Samples;
#else
        return Result::Error;
#endif
    }

#ifdef CARDPUTER_MP3_DECODER
    auto& mp3 = *state->mp3;
    auto& stream = *mp3.stream;
    auto& pcm = mp3.synth->pcm;
    auto* input = mp3.input.get();
    unsigned refills = 0;
    while (mp3.consumed == pcm.length) {
        if (!stream.buffer || stream.error == MAD_ERROR_BUFLEN) {
            if (mp3.eof) return state->readFailed ? Result::Error : Result::End;
            if (++refills > 32) return Result::Error;
            const size_t carry = stream.next_frame ? stream.bufend - stream.next_frame : 0;
            mp3.inputOffset = state->file.position() - carry;
            constexpr size_t capacity = State::Mp3::inputCapacity;
            if (carry >= capacity) return Result::Error;
            if (carry) memmove(input, stream.next_frame, carry);
            size_t count = carry + state->readFile(input + carry, capacity - carry);
            if (state->readFailed) return Result::Error;
            if (state->file.position() >= state->file.size()) {
                mp3.eof = true;
                memset(input + count, 0, MAD_BUFFER_GUARD);
                count += MAD_BUFFER_GUARD;
            }
            mad_stream_buffer(&stream, input, count);
            stream.error = MAD_ERROR_NONE;
        }
        if (mad_frame_decode(mp3.frame.get(), &stream)) {
            if (stream.error == MAD_ERROR_BUFLEN || MAD_RECOVERABLE(stream.error)) continue;
            return Result::Error;
        }
        // Apply headroom before libmad's synthesis clips to 16-bit PCM.
        const unsigned channels = MAD_NCHANNELS(&mp3.frame->header);
        const unsigned slots = MAD_NSBSAMPLES(&mp3.frame->header);
        for (unsigned channel = 0; channel < channels; ++channel) {
            for (unsigned slot = 0; slot < slots; ++slot) {
                for (auto& sample : mp3.frame->sbsample[channel][slot])
                    sample -= sample / (100 / (100 - mp3DecodeGainPercent));
            }
        }
        mad_synth_frame(mp3.synth.get(), mp3.frame.get());
        mp3.consumed = 0;
    }
    block.rate = pcm.samplerate;
    block.mp3Offset = mp3.inputOffset + (stream.this_frame - input);
    block.mp3BytesPerSecond = mp3.frame->header.bitrate / 8;
    if (block.rate < 8000 || block.rate > 48000) return Result::Error;
    block.frames = std::min(blockFrames, size_t(pcm.length) - mp3.consumed);
    for (size_t i = 0; i < block.frames; ++i) {
        const auto& frame = pcm.samplesX[mp3.consumed + i];
        block.stereo[2 * i] = frame[0];
        block.stereo[2 * i + 1] = frame[pcm.channels == 1 ? 0 : 1];
    }
    mp3.consumed += block.frames;
    return Result::Samples;
#endif
}
