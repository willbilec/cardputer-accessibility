#include "global_shortcuts.h"
#include <cassert>
using namespace globalShortcuts;
int main() {
    Key k{2,11,true,true,false,false,false,false};
    assert(route(k)==Command::None);
    k.function=true; assert(route(k)==Command::None);
    k.row=3; assert(route(k)==Command::None);
    k.function=false; assert(route(k)==Command::None);
    k.row=3;k.col=13;assert(route(k)==Command::None);
    k.row=1;k.col=4;assert(route(k)==Command::None);
    k.col=5;assert(route(k)==Command::None);
    k.row=2;k.col=7;assert(route(k)==Command::None);
    k.option=false; k.alt=true; k.row=1; k.col=1;
    assert(route(k)==Command::Home);
    k.row=3; k.col=7; assert(route(k)==Command::Battery);
    k.row=1; k.col=5; assert(route(k)==Command::Time);
    k.col=4; assert(route(k)==Command::Repeat);
    k.row=2; k.col=7; assert(route(k)==Command::Help);
    k.row=3; k.col=13; assert(route(k)==Command::Stop);
    k.row=3; k.col=8; assert(route(k)==Command::Network);
    k.down=false; assert(route(k)==Command::None);
    k.down=true; k.option=true; assert(route(k)==Command::None);
    k.option=false; k.control=true; assert(route(k)==Command::None);
    k.control=false; k.shift=true; assert(route(k)==Command::None);
    k.shift=false; k.row=3; k.col=10; assert(route(k)==Command::RateDown);
    k.col=12; assert(route(k)==Command::RateUp);
    k.col=11; assert(route(k)==Command::CombinedVolumeDown);
    k.row=2; assert(route(k)==Command::CombinedVolumeUp);
    k.function=true; assert(route(k)==Command::None);
    k.function=false;
    k.row=0; k.col=11; assert(route(k)==Command::MediaDown);
    k.col=12; assert(route(k)==Command::MediaUp);
    k.row=1; k.col=11; assert(route(k)==Command::VolumeDown);
    k.col=12; assert(route(k)==Command::VolumeUp);
    k.function=true; assert(route(k)==Command::None);
}
