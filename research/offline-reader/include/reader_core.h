#pragma once
#include <string>

namespace reader {
// The adapter never supplies password content, even to this transient model.
struct Snapshot {
    std::string screen, focus, status;
};
class Observer {
    Snapshot previous;
    bool initialized = false;
public:
    std::string update(const Snapshot &now, bool force = false) {
        std::string result;
        if (force || !initialized || now.screen != previous.screen)
            result = now.screen + ". " + now.focus;
        else if (now.focus != previous.focus) result = now.focus;
        else if (now.status != previous.status) result = now.status;
        previous = now;
        initialized = true;
        return result;
    }
};
// Exact paired layout: never install over the factory reader or arbitrary data.
inline bool validLayout(unsigned factoryAddress, unsigned factorySize,
                        unsigned appAddress, unsigned appSize, unsigned imageSize) {
    return factoryAddress == 0x10000 && factorySize == 0x280000 &&
           appAddress == 0x290000 && appSize == 0x560000 &&
           imageSize > 0 && imageSize <= appSize;
}
}
