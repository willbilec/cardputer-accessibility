from pathlib import Path
import shlex
import subprocess
import sys
if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parent

def main():
    out = ROOT / "test-results"
    out.mkdir(exist_ok=True)
    commands = [("integration", [sys.executable, "test/test_prepare.py"])]
    commands.append(("hid-adaptation",[sys.executable,"test/test_prepare_hid.py"]))
    commands.append(("backgroundaudio-adaptation",[sys.executable,"prepare_background.py"]))
    commands.append(("backgroundaudio-reservation",[sys.executable,"test/test_background_prepare.py"]))
    shell = ["sh", "-lc"]
    directory = ROOT.as_posix()
    if sys.platform == "win32":
        directory = subprocess.check_output(
            ["wsl", "--exec", "wslpath", "-a", directory], text=True
        ).strip()
        shell = ["wsl", "--exec", *shell]
    for name, source, flags in [
        ("core", "test/test_core.cpp", ""),
        ("file-playback-formats", "test/test_file_playback_formats.cpp", "-Iinclude"),
        ("keyboard-app-models", "test/test_keyboard_apps.cpp", ""),
        ("global-shortcuts", "test/test_global_shortcuts.cpp", "-Iinclude"),
        ("synth", "test/test_synth.cpp", "-Iinclude src/synth.cpp"),
        ("playpalace-grid", "test/test_playpalace_grid.cpp", "-Iinclude"),
        ("playpalace-speech", "test/test_playpalace_speech.cpp", "-Iinclude"),
        ("recording-wav-header", "test/test_wav_header.cpp", "-Iinclude"),
        ("settings-codec", "test/test_settings_codec.cpp", "-Iinclude"),
        ("player-adapter", "test/test_player_adapter.cpp", "-Itest/fakes -I../speech-prototype/src"),
        ("installer-transfer", "test/test_transfer.cpp", ""),
        ("installer-runtime", "test/test_installer_runtime.cpp", "-Itest/installer_fakes -Itest/headless_fakes -Itest/fakes -Iinclude -I../speech-prototype/src"),
        ("headless-board", "test/test_headless_board.cpp", "-Itest/headless_fakes -Iinclude"),
        ("audio-mixer", "test/test_audio_mixer.cpp", "-fsanitize=address,undefined -fno-omit-frame-pointer -Itest/headless_fakes -Iinclude"),
        ("backgroundaudio-mixer-units", "test/test_backgroundaudio_units.cpp", "-fsanitize=address,undefined -fno-omit-frame-pointer -Itest/backgroundaudio_fakes"),
        ("hid-runtime", "test/test_hid_runtime.cpp", "-Itest/headless_fakes -Itest/fakes -Iinclude -Igenerated -I../speech-prototype/src"),
        ("hid-chaos", "test/test_hid_chaos.cpp", "-fsanitize=address,undefined -fno-omit-frame-pointer -Itest/headless_fakes -Itest/fakes -Iinclude -Igenerated -I../speech-prototype/src"),
        ("learning-runtime", "test/test_learning_runtime.cpp", "-Itest/headless_fakes -Itest/fakes -Iinclude -I../speech-prototype/src"),
        ("speech-default", "../speech-prototype/test/test_speech_service.cpp", "-I../speech-prototype/test/fakes"),
        ("speech-channel-one", "../speech-prototype/test/test_speech_service.cpp", "-DSPEECH_OUTPUT_CHANNEL=1 -I../speech-prototype/test/fakes"),
    ]:
        command = (
            f"cd {shlex.quote(directory)} && "
            'binary=$(mktemp) && trap \'rm -f "$binary"\' EXIT && '
            f'g++ -std=c++17 -Wall -Wextra -Werror {flags} {source} -o "$binary" && "$binary"'
        )
        commands.append((name, [*shell, command]))
    for name, command in commands:
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        (out / (name + ".txt")).write_text(result.stdout, encoding="utf-8")
        print(name + (": PASS" if result.returncode == 0 else ": FAIL"), flush=True)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)

if __name__ == "__main__": main()
