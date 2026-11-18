#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "../include/image_transfer.h"

int main() {
    std::vector<uint8_t> source(9001), output;
    for (size_t i = 0; i < source.size(); ++i) source[i] = i % 251;
    size_t cursor = 0, latest = 0;
    auto read = [&](uint8_t *p, size_t n) {
        n = std::min({n, size_t(1777), source.size() - cursor});
        std::memcpy(p, source.data() + cursor, n); cursor += n; return int(n);
    };
    auto write = [&](const uint8_t *p, size_t n) { output.insert(output.end(), p, p + n); return true; };
    auto progress = [&](size_t p, size_t n) { assert(p > latest && p <= n); latest = p; };
    assert(transferImage(source.size(), read, write, progress));
    assert(output == source && latest == source.size());
    cursor = 0; latest = 0; output.clear();
    assert(!transferImage(source.size() + 1, read, write, progress)); // truncated SD data
    cursor = 0; latest = 0; output.clear();
    assert(!transferImage(source.size(), read, [](const uint8_t *, size_t) { return false; }, progress));
    assert(latest == 0); // failed flash writes must not be reported as progress
    assert(!transferImage(0, read, write, progress));
    assert(!transferImage(1, [](uint8_t *, size_t n) { return int(n + 1); }, write, progress));
    assert(output.empty()); // oversized reads rejected before flash callback
    std::cout << "Actual installer transfer: partial reads, truncation, write failure and bounds passed\n";
}
