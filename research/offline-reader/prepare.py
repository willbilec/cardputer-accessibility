"""Pinned upstream integration. No edits are made in the downloaded checkout."""
from pathlib import Path
import hashlib
import re

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parent
else:
    Import("env")
    ROOT = Path(env["PROJECT_DIR"])
SOURCE_SHA = "83d65c3934ce4e549f29ad583fe6ff3beeafc8017d073dacfecf82997f328dcb"

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Upstream integration anchor changed: " + old[:80])
    return text.replace(old, new, 1)

def block(text, start):
    opening = text.index("{", start)
    depth = 1
    pos = opening + 1
    while depth:
        if text[pos] == "{": depth += 1
        elif text[pos] == "}": depth -= 1
        pos += 1
    return text[start:pos]

def generate_player(headless=False):
    source = ROOT.parent / "MP3PlayerForM5Cardputer/MP3PlayerM5Cardputer.ino"
    raw = source.read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA:
        raise ValueError("Player source differs from reviewed revision; refusing automatic adaptation")
    text = raw.decode("utf-8")
    text = '#include "speech_service.h"\nstatic void readerNotice(const char *);\nstatic unsigned readerLibraryGeneration = 0;\nstatic int readerMusicVolume = 128;\n' + text
    text = replace_once(text,
        'auto cfg = M5.config(); cfg.external_speaker.hat_spk = true; M5Cardputer.begin(cfg);',
        'Serial.begin(115200); auto cfg = M5.config(); cfg.fallback_board = m5::board_t::board_M5CardputerADV; M5Cardputer.begin(cfg, true);\n'
        '    M5Cardputer.Speaker.setVolume(128);')
    text = replace_once(text,
        'if (nvs_flash_init() != ESP_OK) { nvs_flash_erase(); nvs_flash_init(); }',
        'if (nvs_flash_init() != ESP_OK) { speechServiceBegin(); readerFatal("Settings unavailable. No data erased. Control Q returns to launcher."); }')
    text = replace_once(text, '\n    M5Cardputer.Speaker.config(spk_cfg);',
        '\n    M5Cardputer.Speaker.config(spk_cfg);\n    speechServiceBegin(); readerNotice("Starting accessible media player");')
    text = replace_once(text, 'ConfigManager::load(); applyCpuFrequency();',
        'ConfigManager::load(); userSettings.resumePlay = false; userSettings.spkRateIndex = 0; applyCpuFrequency();')
    text = replace_once(text, 'if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) while(1);',
        'if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) readerFatal("SD card unavailable. Control Q returns to launcher.");')
    text = replace_once(text, 'SplashScreen::show();', '// Animated splash/USB disk boot menu omitted in this offline test build.')
    # Separate app namespace and caches prevent overwriting the original player's files.
    text = text.replace('"sam_music"', '"reader_music"')
    text = text.replace('"/playlist.txt"', '"/reader-playlist.txt"').replace('"/config.txt"', '"/reader-config.txt"')
    text = text.replace('"/pl_"', '"/reader-pl_"')
    text = replace_once(text, 'if (textInputTarget == 0) { displayStr = "";',
        'if (textInputTarget == 0 || textInputTarget == 2) { displayStr = "";')
    # Shared setting descriptions: drawing and speech use the same switch.
    switch = block(text, text.index('switch (idx)', text.index('static void drawSettings()')))
    label_switch = switch.replace('M5Cardputer.Display.', 'capture.')
    label_switch = label_switch.replace('if (flabel.length() > 12) flabel = flabel.substring(0, 12) + "~";', '')
    helper = '''class ReaderText : public Print {
public:
    String value;
    size_t write(uint8_t c) override { if (value.length() < 512) value += char(c); return 1; }
};
static String readerSettingsLabel(int idx) {
    ReaderText capture;
''' + label_switch + '\nreturn capture.value;\n}\n'
    text = replace_once(text, switch, 'M5Cardputer.Display.print(readerSettingsLabel(idx));')
    text = replace_once(text, 'class UIManager {', helper + '\nclass UIManager {')
    text = replace_once(text, '\nvoid setup() {', '\n#include "player_access.h"\n\nvoid setup() {')
    text = replace_once(text, '\nvoid loop() {', '\nstatic void playerLoop() {')
    text = replace_once(text, 'UIManager::drawBaseUI(); lastInputTime = millis();',
        'if (audioApp.decoder && !audioApp.isPaused) audioApp.togglePause();\n    UIManager::drawBaseUI(); lastInputTime = millis(); readerPlaybackReady = true;')
    text = replace_once(text, 'M5Cardputer.update(); server.handleClient(); audioApp.loopTasks();',
        'server.handleClient(); audioApp.loopTasks();')
    text = replace_once(text, 'M5Cardputer.Speaker.setVolume(min(255, M5Cardputer.Speaker.getVolume() + 10))',
        'readerMusicVolume = min(255, readerMusicVolume + 10)')
    text = replace_once(text, 'M5Cardputer.Speaker.setVolume(max(0, M5Cardputer.Speaker.getVolume() - 10))',
        'readerMusicVolume = max(0, readerMusicVolume - 10)')
    text = text.replace('M5Cardputer.Speaker.getVolume()', 'readerMusicVolume')
    text = replace_once(text, 'int maxScroll = max(0, (int)I18N::help.size() - 7);',
        'int maxScroll = max(0, (int)I18N::help.size() - 1);')
    # Empty libraries otherwise cause a modulo-by-zero in the upstream navigation handlers.
    text = text.replace("else if (M5Cardputer.Keyboard.isKeyPressed(';')) { audioApp.browserIndex", "else if (M5Cardputer.Keyboard.isKeyPressed(';') && !audioApp.songOffsets.empty()) { audioApp.browserIndex")
    text = text.replace("else if (M5Cardputer.Keyboard.isKeyPressed('.')) { audioApp.browserIndex", "else if (M5Cardputer.Keyboard.isKeyPressed('.') && !audioApp.songOffsets.empty()) { audioApp.browserIndex")
    # Capture transient messages and scan boundaries, not individual filenames/redraws.
    text = replace_once(text, 'void performFullScan() {', 'void performFullScan() {\n        ++readerLibraryGeneration; readerNotice("Scanning music library");')
    text = replace_once(text, 'void performFolderScan(const String& folder) {', 'void performFolderScan(const String& folder) {\n        ++readerLibraryGeneration; readerNotice("Scanning folder");')
    text = replace_once(text, 'M5Cardputer.Display.print("Must be 8+ chars!");', 'readerNotice("Password must be at least eight characters"); M5Cardputer.Display.print("Must be 8+ chars!");')
    text = text.replace('ConfigManager::save(); ESP.restart();', 'ConfigManager::save(); speechSayAndWait("Settings saved. Restarting player", 2500); ESP.restart();')
    text += '''
void loop() {
    M5Cardputer.update();
    if (readerHandleKeys()) { readerPoll(); delay(2); return; }
    playerLoop();
    readerPoll();
    delay(1);
}
'''
    if headless:
        text = text.replace('#include <M5Unified.h>', '').replace('#include <M5Cardputer.h>', '#include "headless_board.h"')
        # Omit the unused visual/USB boot menus, not just their entry points.
        start = text.index('class BootMenu {')
        end = text.index('#include "player_access.h"', start)
        text = text[:start] + text[end:]
        fonts = block(text, text.index('class UIFonts {'))
        text = replace_once(text, fonts + ';', '')
        fft = block(text, text.index('class fft_t {'))
        text = replace_once(text, fft + ';', '')
        text = replace_once(text, 'static fft_t fft;', '')
        text = replace_once(text, 'static int16_t raw_data[WAVE_SIZE * 2];', '')
        # Retain UI state, cursors and descriptor logic; discard renderer bodies.
        for match in reversed(list(re.finditer(r'static void draw\w+\([^)]*\)\s*\{', text))):
            body = block(text, match.start())
            declaration = body[:body.index('{')]
            text = text[:match.start()] + declaration + '{}' + text[match.start() + len(body):]
        text = text.replace('LGFX_Sprite ', 'HeadlessSprite ')
        text = replace_once(text,
            'auto cfg = M5.config(); cfg.fallback_board = m5::board_t::board_M5CardputerADV; M5Cardputer.begin(cfg, true);',
            'M5Cardputer.begin();')
        text = text.replace('"reader_music"', '"headless_music"')
        text = '#include "headless_settings.h"\n' + text
        text = replace_once(text, 'int UIManager::settingsCursor = 0;', 'int UIManager::settingsCursor = 4;')
        text = replace_once(text, '(UIManager::settingsCursor - 1 + numSettings) % numSettings',
            'headlessNextSetting(UIManager::settingsCursor, -1)')
        text = replace_once(text, '(UIManager::settingsCursor + 1) % numSettings',
            'headlessNextSetting(UIManager::settingsCursor, 1)')
        text = replace_once(text, 'ConfigManager::load(); userSettings.resumePlay = false;',
            'ConfigManager::load(); userSettings.timeoutIndex = 0; userSettings.offVisFullscreen = false; userSettings.resumePlay = false;')
        assert 'fonts::' not in text and 'lgfx::' not in text and 'M5.config' not in text
    return text

def prepare(environment):
    output = ROOT / "generated"
    output.mkdir(exist_ok=True)
    if environment in ("backgroundaudio-minimal", "backgroundaudio-coexist", "unified-dev", "unified",
                       "unified-playpalace"):
        import runpy
        runpy.run_path(str(ROOT / "prepare_background.py"))["prepare"]()
    if environment in ("emulator-klatt", "emulator-stress", "emulator-ble", "velxio-audio-compare", "velxio-audio-pipeline", "headless-klatt-player", "headless-klatt-launcher", "accessible-hid", "key-learning"):
        core = ROOT.parent / "moonshine/micro"
        env.Append(CPPPATH=[str(core / "klatt-tts/include"), str(core / "klatt-tts/include/tts"),
            str(core / "klatt-tts/src"), str(core / "g2p/include"), str(core / "g2p/include/g2p"), str(core / "g2p/src")])
        env.BuildSources("$BUILD_DIR/klatt-core", str(core / "klatt-tts/src"))
        env.BuildSources("$BUILD_DIR/klatt-g2p", str(core / "g2p/src"))
    if environment in ("accessible-hid", "unified-dev", "unified",
                       "unified-playpalace"):
        import importlib.util
        spec = importlib.util.spec_from_file_location("prepare_hid", ROOT / "prepare_hid.py")
        hid = importlib.util.module_from_spec(spec); spec.loader.exec_module(hid); hid.prepare()
    if environment == "headless-klatt-launcher":
        import runpy
        records = runpy.run_path(str(ROOT / "programs.py"))["PROGRAMS"]
        lines=['#pragma once', '#include "program_catalogue.h"']
        for index,(program_environment,program_id,label) in enumerate(records):
            raw=(ROOT / ".pio/build" / program_environment / "firmware.bin").read_bytes()
            if not 0 < len(raw) <= 0x560000: raise ValueError("Program does not fit: "+program_id)
            digest=hashlib.sha256(raw).digest()
            lines.append('static const uint8_t programSha'+str(index)+'[32] = {'+','.join(map(str,digest))+'};')
        lines.append('static const ProgramRecord programs[] = {')
        for index,(program_environment,program_id,label) in enumerate(records):
            size=(ROOT / ".pio/build" / program_environment / "firmware.bin").stat().st_size
            lines.append('{"'+program_id+'","'+label+'","/accessible/'+program_id+'.bin",'+str(size)+',programSha'+str(index)+'},')
        lines += ['};','static constexpr unsigned kProgramCount = sizeof(programs)/sizeof(programs[0]);']
        (output / "program_catalogue_generated.h").write_text('\n'.join(lines)+'\n',encoding='utf-8')
    elif environment == "headless-klatt-player":
        (output / "headless_player_generated.h").write_text(generate_player(headless=True), encoding="utf-8")
    elif environment == "accessible-media-player":
        (output / "player_generated.h").write_text(generate_player(), encoding="utf-8")
    elif environment == "offline-test-launcher":
        player_environment = "accessible-media-player"
        image = ROOT / ".pio/build" / player_environment / "firmware.bin"
        raw = image.read_bytes()
        if not 0 < len(raw) <= 0x560000:
            raise ValueError("Player does not fit the dedicated slot")
        digest = hashlib.sha256(raw).digest()
        (output / "bundle.h").write_text(
            '#pragma once\nstatic constexpr unsigned kPlayerSize = ' + str(len(raw)) + ';\n'
            'static const unsigned char kPlayerSha[32] = {' + ','.join(map(str, digest)) + '};\n', encoding="utf-8")

if __name__ == "__main__":
    prepare("accessible-media-player")
else:
    Import("env")
    prepare(env["PIOENV"])
