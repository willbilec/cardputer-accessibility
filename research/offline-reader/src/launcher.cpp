#include <Arduino.h>
#ifdef READER_HEADLESS
#include "headless_board.h"
#else
#include <M5Cardputer.h>
#endif
#include <SD.h>
#include <SPI.h>
#include <mbedtls/sha256.h>
#include "speech_service.h"
#include "firmware_layout.h"
#include "bundle.h"
#include "image_transfer.h"

namespace {
constexpr const char *imagePath = "/accessible/media-player.bin";
const char *items[] = {"Run installed media player", "Install media player from SD", "Help"};
int selection = 0;
bool confirmation = false;
bool installedVerified = false;

void announce(const char *message) {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("Offline speech test launcher");
    M5Cardputer.Display.println(message);
    speechSay(message);
}
void menu() { announce(items[selection]); }
bool mounted() {
    SPI.begin(40, 39, 14, 12);
    return SD.begin(12, SPI, 25000000);
}
// Verify exactly the packaged file, not a firmware merely sharing its filename.
bool hashFile(File &file) {
    if (file.size() != kPlayerSize || !file.seek(0)) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t data[4096], digest[32];
    size_t total = 0;
    while (ok && total < kPlayerSize) {
        int n = file.read(data, min(sizeof(data), size_t(kPlayerSize - total)));
        if (n <= 0) { ok = false; break; }
        ok = mbedtls_sha256_update(&ctx, data, n) == 0;
        total += n;
        delay(1);
    }
    if (ok) ok = mbedtls_sha256_finish(&ctx, digest) == 0 && !memcmp(digest, kPlayerSha, 32);
    mbedtls_sha256_free(&ctx);
    return ok && total == kPlayerSize;
}
bool verifyInstalled() {
    if (!readerLayoutValid(kPlayerSize)) return false;
    const auto *slot = readerPlayer();
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t data[4096], digest[32];
    size_t offset = 0;
    while (ok && offset < kPlayerSize) {
        size_t n = min(sizeof(data), size_t(kPlayerSize - offset));
        ok = esp_partition_read(slot, offset, data, n) == ESP_OK &&
             mbedtls_sha256_update(&ctx, data, n) == 0;
        offset += n;
        delay(1);
    }
    if (ok) ok = mbedtls_sha256_finish(&ctx, digest) == 0 && !memcmp(digest, kPlayerSha, 32);
    mbedtls_sha256_free(&ctx);
    return ok;
}
void install() {
    const auto *running = esp_ota_get_running_partition();
    if (!readerLayoutValid(kPlayerSize) || !running || running->address != readerFactory()->address) {
        announce("Different flash layout. Installation refused. No partitions changed"); return;
    }
    if (!mounted()) { announce("SD card unavailable. Insert card and try again"); return; }
    File file = SD.open(imagePath, FILE_READ);
    announce("Checking packaged player image");
    if (!file || !hashFile(file)) { announce("Missing or different player image. Installation refused"); return; }
    if (!file.seek(0)) { announce("SD read failed. Installation refused"); return; }
    // No partition table, NVS, factory application or filesystem is erased here.
    // Only the dedicated player slot is written after explicit confirmation.
    esp_ota_handle_t handle;
    if (esp_ota_begin(readerPlayer(), kPlayerSize, &handle) != ESP_OK) {
        announce("Could not start installation"); return;
    }
    installedVerified = false;
    int lastMilestone = -1;
    bool ok = transferImage(kPlayerSize,
      [&](uint8_t *buffer, size_t n) { return file.read(buffer, n); },
      [&](const uint8_t *buffer, size_t n) { return esp_ota_write(handle, buffer, n) == ESP_OK; },
      [&](size_t offset, size_t total) {
        int milestone = (offset * 4 / total) * 25;
        if (milestone != lastMilestone) {
            lastMilestone = milestone;
            char text[48]; snprintf(text, sizeof(text), "Installing player. %d percent", milestone);
            announce(text);
        }
        delay(1);
      });
    file.close();
    if (!ok) {
        esp_ota_abort(handle);
        announce("Installation failed. Launcher preserved. Retry from SD"); return;
    }
    if (esp_ota_end(handle) != ESP_OK || !verifyInstalled()) {
        announce("Player verification failed. Launcher preserved. Retry from SD"); return;
    }
    installedVerified = true;
    selection = 0;
    announce("Player installed and verified. Enter runs it");
}
void run() {
    announce("Checking installed player");
    if (!installedVerified) installedVerified = verifyInstalled();
    if (!installedVerified) { announce("Packaged player is not installed. Choose Install from SD"); return; }
    if (esp_ota_set_boot_partition(readerPlayer()) != ESP_OK) {
        announce("Player image is not bootable. Staying in launcher"); return;
    }
    speechSayAndWait("Starting accessible media player. Control Q returns here", 4000);
    ESP.restart();
}
}
void setup() {
    Serial.begin(115200);
#ifdef READER_HEADLESS
    M5Cardputer.begin();
#else
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5CardputerADV;
    M5Cardputer.begin(cfg, true);
#endif
    M5Cardputer.Speaker.setVolume(128);
    M5Cardputer.Display.setRotation(1);
    speechServiceBegin();
    announce("Offline launcher. Semicolon and period move. Enter selects. R repeats. Space stops. Run installed media player");
}
void loop() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) { delay(5); return; }
    auto keys = M5Cardputer.Keyboard.keysState();
    if (keys.space) { speechStop(); return; }
    if (M5Cardputer.Keyboard.isKeyPressed('r')) { speechRepeat(); return; }
    if (confirmation) {
        confirmation = false;
        if (keys.enter) install();
        else announce("Installation cancelled");
        return;
    }
    if (M5Cardputer.Keyboard.isKeyPressed(';')) { selection = (selection + 2) % 3; menu(); }
    else if (M5Cardputer.Keyboard.isKeyPressed('.')) { selection = (selection + 1) % 3; menu(); }
    else if (keys.enter) {
        if (selection == 0) run();
        else if (selection == 1) {
            confirmation = true;
            announce("Replace the test player slot with our SD image? Enter confirms. Any other key cancels. Launcher and settings are preserved");
        } else announce("Only our speech-enabled player is supported. Copy accessible media-player dot bin onto SD. Install, then Run. Player Control Q returns here");
    }
}
