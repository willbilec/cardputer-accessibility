#pragma once
#include <cstdint>

// Use physical key positions and reserve shared reader chords for Alt.
namespace globalShortcuts {
enum class Command { None, VolumeUp, VolumeDown, RateDown, RateUp,
                     CombinedVolumeUp, CombinedVolumeDown, MediaUp, MediaDown,
                     Home, Battery, Time, Network, Stop, Repeat, Help };
struct Key { uint8_t row, col; bool down, option, function, alt, control, shift; };
inline Command route(const Key &k) {
    if (k.down && k.alt && !k.option && !k.function && !k.control && !k.shift) {
        if (k.row == 0 && k.col == 11) return Command::MediaDown; // Minus
        if (k.row == 0 && k.col == 12) return Command::MediaUp; // Equals
        if (k.row == 1 && k.col == 11) return Command::VolumeDown; // Left bracket
        if (k.row == 1 && k.col == 12) return Command::VolumeUp; // Right bracket
        if (k.row == 2 && k.col == 11) return Command::CombinedVolumeUp;
        if (k.row == 3 && k.col == 11) return Command::CombinedVolumeDown;
        if (k.row == 3 && k.col == 10) return Command::RateDown;
        if (k.row == 3 && k.col == 12) return Command::RateUp;
        if (k.row == 1 && k.col == 1) return Command::Home; // Q
        if (k.row == 3 && k.col == 7) return Command::Battery;
        if (k.row == 1 && k.col == 5) return Command::Time;
        if (k.row == 3 && k.col == 8) return Command::Network;
        if (k.row == 3 && k.col == 13) return Command::Stop;
        if (k.row == 1 && k.col == 4) return Command::Repeat;
        if (k.row == 2 && k.col == 7) return Command::Help;
    }
    return Command::None;
}

}
