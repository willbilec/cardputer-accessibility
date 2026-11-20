#include "playpalace_sound.h"
#include "playpalace_adpcm.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>
#include <ogg/ogg.h>
#include "media_decoder.h"
#include <algorithm>
#include <atomic>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace playpalace::sound {
namespace {
constexpr size_t maxDownload = 4 * 1024 * 1024;
struct Effect {
    fs::FS* storage = nullptr;
    String url, cache, temp;
    StreamBufferHandle_t pcm = nullptr;
    std::atomic<unsigned> rate{0};
    std::atomic<bool> stop{false}, done{false};
    std::atomic<unsigned> volume{100};
    size_t sampleBytes = 0;
    bool wav = false;
};
struct Reader {
    Effect& effect;
    File file;
    esp_http_client_handle_t http = nullptr;
    size_t bytes = 0;
    bool failed = false;

    size_t read(void* target, size_t count) {
        if (effect.stop || failed) return 0;
        if (!http) return file.read(static_cast<uint8_t*>(target), count);
        if (esp_http_client_is_complete_data_received(http)) return 0;
        const int size = esp_http_client_read(http, static_cast<char*>(target), count);
        if (size <= 0) {
            failed = !esp_http_client_is_complete_data_received(http);
            return 0;
        }
        bytes += size;
        if (bytes > maxDownload || file.write(static_cast<uint8_t*>(target), size) != size_t(size)) {
            failed = true;
            return 0;
        }
        return size;
    }

    bool exact(uint8_t* target, size_t length) {
        while (length) {
            const auto count = read(target, length);
            if (!count) return false;
            target += count;
            length -= count;
        }
        return true;
    }
};

std::unique_ptr<Effect> effect;
struct Request { fs::FS* storage; String base, name; unsigned volume; };
std::optional<Request> replacement;
bool discardAudio = false;
uint8_t pending[512];
size_t pendingBytes = 0, pendingOffset = 0;
int bank = -1;
size_t bankRemaining = 0, bankPadding = 0;
unsigned bankVolume = 100;
Adpcm bankDecoder;
#ifdef UNIFIED_DIAGNOSTICS
uint32_t bankStarts = 0, bankOutputBytes = 0;
uint32_t networkStarts = 0;
#endif

unsigned word(const uint8_t* bytes) { return bytes[0] | unsigned(bytes[1]) << 8; }
unsigned dword(const uint8_t* bytes) { return word(bytes) | word(bytes + 2) << 16; }

bool startBank(fs::FS& storage, const String& name, unsigned volume) {
    const char* mount = storage.mountpoint();
    if (!mount) return false;
    char path[96];
    if (snprintf(path, sizeof(path), "%s/PlayPalace/effects.ppfx", mount) >= int(sizeof(path))) return false;
    const int file = ::open(path, O_RDONLY);
    if (file < 0) return false;
    struct BankFile {
        int descriptor;
        ~BankFile() { if (descriptor >= 0) ::close(descriptor); }
    } opened{file};
    struct stat info{};
    uint8_t header[8];
    if (fstat(file, &info) != 0 || info.st_size < 8 ||
        ::read(file, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "PPF2", 4)) return false;
    const unsigned count = dword(header + 4);
    if (!count || count > 1024) return false;
    uint8_t digest[32], item[24];
    if (mbedtls_sha256(reinterpret_cast<const uint8_t*>(name.c_str()), name.length(), digest, 0))
        return false;
    unsigned first = 0, last = count;
    while (first < last) {
        const unsigned position = first + (last - first) / 2;
        if (lseek(file, 8 + position * sizeof(item), SEEK_SET) < 0 ||
            ::read(file, item, sizeof(item)) != sizeof(item))
            return false;
        const int order = memcmp(item, digest, 16);
        if (order < 0) { first = position + 1; continue; }
        if (order > 0) { last = position; continue; }
        const unsigned at = dword(item + 16), samples = dword(item + 20);
        const unsigned size = (samples + 1) / 2;
        if (!samples || samples > 64000 || at < 8 + count * sizeof(item) ||
            at > uint32_t(info.st_size) || size > uint32_t(info.st_size) - at ||
            lseek(file, at, SEEK_SET) < 0) return false;
        bank = file;
        opened.descriptor = -1;
        bankRemaining = samples;
        bankPadding = (1024 - (samples * 4) % 1024) % 1024;
        bankVolume = std::min(volume, 100u);
        bankDecoder = {};
#ifdef UNIFIED_DIAGNOSTICS
        ++bankStarts;
#endif
        return true;
    }
    return false;
}

void bufferPcm(Effect& effect, const uint8_t* outgoing, size_t remaining) {
    while (remaining && !effect.stop) {
        const auto count = xStreamBufferSend(effect.pcm, outgoing, remaining, pdMS_TO_TICKS(20));
        remaining -= count;
        outgoing += count;
        effect.sampleBytes += count;
    }
}

void queue(Effect& effect, const uint8_t* bytes, size_t length, unsigned channels) {
    int16_t stereo[512];
    const int volume = effect.volume;
    const size_t frames = std::min(length / (2 * channels), size_t(256));
    for (size_t i = 0; i < frames; ++i) {
        const auto left = int16_t(word(bytes + 2 * i * channels));
        const auto right = channels == 1 ? left : int16_t(word(bytes + 4 * i + 2));
        stereo[2 * i] = int(left) * volume / 100;
        stereo[2 * i + 1] = int(right) * volume / 100;
    }
    bufferPcm(effect, reinterpret_cast<const uint8_t*>(stereo), frames * 4);
}

bool wav(Reader& reader) {
    uint8_t format[16], header[12], buffer[512];
    if (!reader.exact(header, sizeof(header)) || memcmp(header, "RIFF", 4)
        || memcmp(header + 8, "WAVE", 4)) return false;
    unsigned channels = 0;
    for (unsigned chunks = 0; chunks != 64 && reader.exact(header, 8); ++chunks) {
        unsigned length = dword(header + 4);
        if (length > maxDownload) return false;
        if (!memcmp(header, "fmt ", 4)) {
            if (length < 16 || !reader.exact(format, sizeof(format))) return false;
            const unsigned rate = dword(format + 4);
            channels = word(format + 2);
            if (word(format) != 1 || word(format + 14) != 16 ||
                channels < 1 || channels > 2 || rate < 8000 || rate > 48000) return false;
            reader.effect.rate = rate;
            length -= 16;
        } else if (!memcmp(header, "data", 4)) {
            if (!channels || length % (2 * channels)) return false;
            while (length) {
                const unsigned part = std::min(length, unsigned(sizeof(buffer)));
                if (!reader.exact(buffer, part)) return false;
                queue(reader.effect, buffer, part, channels);
                length -= part;
            }
            return true;
        }
        length += length % 2;
        while (length) {
            const unsigned part = std::min(length, unsigned(sizeof(buffer)));
            if (!reader.exact(buffer, part)) return false;
            length -= part;
        }
    }
    return false;
}

bool vorbis(Reader& reader) {
    const auto read = [](void* context, uint8_t* destination, size_t capacity) {
        auto& source = *static_cast<Reader*>(context);
        const auto count = source.read(destination, capacity);
        return source.failed ? -1 : int(count);
    };
    MediaDecoder decoder;
    if (!decoder.openVorbis(read, &reader)) return false;
    MediaDecoder::Block block;
    while (!reader.effect.stop) {
        const auto result = decoder.read(block);
        if (result == MediaDecoder::Result::End) return reader.effect.rate && !reader.failed;
        if (result == MediaDecoder::Result::Error ||
            (reader.effect.rate && block.rate != reader.effect.rate)) return false;
        reader.effect.rate = block.rate;
        queue(reader.effect, reinterpret_cast<const uint8_t*>(block.stereo), block.frames * 4, 2);
    }
    return false;
}

void worker(void* argument) {
    auto& effect = *static_cast<Effect*>(argument);
    {
        Reader reader{effect, {}};
        auto& storage = *effect.storage;
        const bool cached = storage.exists(effect.cache);
        if (cached) reader.file = storage.open(effect.cache);
        else {
            storage.remove(effect.temp);
            reader.file = storage.open(effect.temp, FILE_WRITE);
            if (reader.file) {
                esp_http_client_config_t config{};
                config.url = effect.url.c_str();
                config.timeout_ms = 3500;
                config.buffer_size = 1024;
                config.buffer_size_tx = 640;
                config.disable_auto_redirect = true;
                config.crt_bundle_attach = esp_crt_bundle_attach;
                reader.http = esp_http_client_init(&config);
                if (!reader.http || esp_http_client_open(reader.http, 0) != ESP_OK) reader.failed = true;
                else {
                    const auto length = esp_http_client_fetch_headers(reader.http);
                    reader.failed = length < 0 || length > maxDownload ||
                        esp_http_client_get_status_code(reader.http) != 200;
                }
            }
        }
        bool complete = reader.file && !reader.failed && (effect.wav ? wav(reader) : vorbis(reader));
        if (!effect.stop && effect.sampleBytes % 1024) {
            static const uint8_t silence[1024]{};
            bufferPcm(effect, silence, 1024 - effect.sampleBytes % 1024);
        }
        if (reader.http) {
            uint8_t chunk[256];
            while (complete && reader.read(chunk, sizeof(chunk))) {}
            complete = complete && !effect.stop && !reader.failed &&
                esp_http_client_is_complete_data_received(reader.http);
            esp_http_client_cleanup(reader.http);
        }
        reader.file.close();
        if (!cached) {
            if (complete && !storage.exists(effect.cache)) storage.rename(effect.temp, effect.cache);
            storage.remove(effect.temp);
        }
    }
    effect.done = true;
    vTaskDelete(nullptr);
}

String encodedPath(const String& name) {
    if (name.isEmpty() || name.length() > 180 || name[0] == '/') return {};
    const char* hex = "0123456789abcdef";
    String result;
    int segment = 0;
    for (unsigned i = 0; i <= name.length(); ++i) {
        const uint8_t c = name[i];
        if (c == '/' || !c) {
            const String component = name.substring(segment, i);
            if (component.isEmpty() || component == "." || component == "..") return {};
            segment = i + 1;
            if (c) result += '/';
        } else if (isalnum(c) || c == '.' || c == '-' || c == '_') result += char(c);
        else {
            if (c == '\\' || c < 32) return {};
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
}

void request(fs::FS& storage, const String& base, const String& name, unsigned volume) {
    if (!volume || (!base.startsWith("https://") && !base.startsWith("http://"))) return;
    const bool wavFile = name.endsWith(".wav");
    if (!wavFile && !name.endsWith(".ogg")) return;
    const String path = encodedPath(name);
    if (path.isEmpty()) return;
    cancel();
    if (startBank(storage, name, volume)) return;
    if (effect) {
        replacement = Request{&storage, base, name, volume};
        return;
    }
    // Playback is optional when the decoder would compete with speech and Wi-Fi.
    const unsigned requiredHeap = base.startsWith("https://") ? 60000 : wavFile ? 36000 : 48000;
    if (ESP.getFreeHeap() < requiredHeap ||
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 12544) return;
    auto next = std::make_unique<Effect>();
    next->storage = &storage;
    next->url = base + (base.endsWith("/") ? "" : "/") + path;
    unsigned char digest[32];
    if (mbedtls_sha256(reinterpret_cast<const uint8_t*>(next->url.c_str()),
                       next->url.length(), digest, 0)) return;
    char hash[65];
    for (unsigned i = 0; i < sizeof(digest); ++i) snprintf(hash + i * 2, 3, "%02x", digest[i]);
    if (!storage.exists("/PlayPalace/sounds") && !storage.mkdir("/PlayPalace/sounds")) return;
    next->cache = String("/PlayPalace/sounds/") + hash + (wavFile ? ".wav" : ".ogg");
    next->temp = next->cache + ".part";
    next->wav = wavFile;
    next->volume = std::min(100u, volume);
    next->pcm = xStreamBufferCreate(4096, 4);
    if (!next->pcm) return;
    if (xTaskCreate(worker, "PlayPalace FX", 12288, next.get(), 1, nullptr) != pdPASS) {
        vStreamBufferDelete(next->pcm);
        return;
    }
    effect = std::move(next);
#ifdef UNIFIED_DIAGNOSTICS
    ++networkStarts;
#endif
}

void cancel() {
    discardAudio = true;
    if (effect) effect->stop = true;
    if (bank >= 0) { ::close(bank); bank = -1; }
    replacement.reset();
    pendingBytes = pendingOffset = 0;
}
bool busy(){return discardAudio||effect||bank>=0;}

void changeVolume(unsigned previous, unsigned current) {
    if (!current) { cancel(); return; }
    if (!previous) return;
    const auto scale = [previous, current](unsigned volume) {
        return std::min(100u, volume * current / previous);
    };
    if (bank >= 0) bankVolume = scale(bankVolume);
    if (effect) effect->volume = scale(effect->volume.load());
    if (replacement) replacement->volume = scale(replacement->volume);
}

void tick(Output output, void (*discard)()) {
    if (discardAudio) {
        discard();
        discardAudio = false;
    }
    if (effect && effect->done && (effect->stop ||
        (pendingOffset == pendingBytes && xStreamBufferIsEmpty(effect->pcm)))) {
        vStreamBufferDelete(effect->pcm);
        effect.reset();
        if (replacement) {
            const Request next = std::move(*replacement);
            replacement.reset();
            request(*next.storage, next.base, next.name, next.volume);
            // Apply the discard requested by request() before writing the new effect.
            return;
        }
    }
    if (bank >= 0) {
        if (pendingOffset == pendingBytes) {
            pendingOffset = pendingBytes = 0;
            if (bankRemaining) {
                uint8_t compressed[sizeof(pending) / 8];
                const size_t samples = std::min(bankRemaining, sizeof(pending) / 4);
                const size_t count = (samples + 1) / 2;
                if (::read(bank, compressed, count) != ssize_t(count)) {
                    bankRemaining = bankPadding = 0;
                } else {
                    for (size_t i = 0; i < samples; ++i) {
                        const unsigned code = compressed[i / 2] >> (i % 2 * 4) & 15;
                        const uint16_t sample = int(bankDecoder.decode(code)) * int(bankVolume) / 100;
                        pending[4 * i] = pending[4 * i + 2] = sample & 255;
                        pending[4 * i + 1] = pending[4 * i + 3] = sample >> 8;
                    }
                    bankRemaining -= samples;
                    pendingBytes = samples * 4;
                }
            } else if (bankPadding) {
                pendingBytes = std::min(bankPadding, sizeof(pending));
                memset(pending, 0, pendingBytes);
                bankPadding -= pendingBytes;
            }
        }
        if (pendingBytes > pendingOffset) {
            const auto written = output(16000, pending + pendingOffset, pendingBytes - pendingOffset);
            pendingOffset += written;
#ifdef UNIFIED_DIAGNOSTICS
            bankOutputBytes += written;
#endif
        }
        if (!bankRemaining && !bankPadding && pendingOffset == pendingBytes) {
            ::close(bank);
            bank = -1;
        }
        return;
    }
    if (!effect) return;
    if (ESP.getFreeHeap() < 9000) effect->stop = true;
    if (!effect->stop && effect->rate) {
        if (pendingOffset == pendingBytes) {
            pendingOffset = 0;
            pendingBytes = xStreamBufferReceive(effect->pcm, pending, sizeof(pending), 0);
        }
        if (pendingBytes > pendingOffset)
            pendingOffset += output(effect->rate, pending + pendingOffset, pendingBytes - pendingOffset);
    }
}

#ifdef UNIFIED_DIAGNOSTICS
void status() {
    Serial.printf("PPFX bank=%u output=%u network=%u active=%u\n", bankStarts, bankOutputBytes,
                  networkStarts, unsigned(bool(effect)));
}
#endif
}
