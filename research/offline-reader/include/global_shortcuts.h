#pragma once
#include <cstdint>

// The physical key position is used so the Function arrow layer does not
// obscure the distinction between reader volume and speech-rate commands.
namespace globalShortcuts {
enum class Command { None, VolumeUp, VolumeDown, RateDown, RateUp,
                     CombinedVolumeUp, CombinedVolumeDown,
                     Home, Battery, Time, Network, Stop, Repeat, Help };
struct Key { uint8_t row, col; bool down, option, function, alt, control, shift; };
inline Command route(const Key &k) {
    if (!k.down || k.control || k.shift) return Command::None;
    if (k.option && !k.alt) {
        if (k.row == 2 && k.col == 11)
            return k.function ? Command::VolumeUp : Command::RateDown;
        if (k.row == 3 && k.col == 11)
            return k.function ? Command::VolumeDown : Command::RateUp;
        if (!k.function && k.row == 3 && k.col == 13) return Command::Stop;
        if (!k.function && k.row == 1 && k.col == 4) return Command::Repeat;
        if (!k.function && k.row == 2 && k.col == 7) return Command::Help;
    }
    if (k.alt && !k.option && !k.function) {
        if (k.row == 2 && k.col == 11) return Command::CombinedVolumeUp;
        if (k.row == 3 && k.col == 11) return Command::CombinedVolumeDown;
        if (k.row == 3 && k.col == 10) return Command::RateDown;
        if (k.row == 3 && k.col == 12) return Command::RateUp;
        if (k.row == 1 && k.col == 1) return Command::Home; // Q
        if (k.row == 3 && k.col == 7) return Command::Battery;
        if (k.row == 1 && k.col == 5) return Command::Time;
        if (k.row == 3 && k.col == 8) return Command::Network;
    }
    return Command::None;
}
}
