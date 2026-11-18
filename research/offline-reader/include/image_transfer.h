#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>

// Read/write adapters report a positive byte count and success respectively.
// Bounds are checked before forwarding any bytes to flash.
template<size_t BufferSize=4096, class Read, class Write, class Progress>
bool transferImage(size_t size, Read read, Write write, Progress progress) {
    if (!size) return false;
    static_assert(BufferSize>0,"Transfer buffer cannot be empty");
    uint8_t buffer[BufferSize];
    size_t offset = 0;
    while (offset < size) {
        size_t requested = std::min(sizeof(buffer), size - offset);
        int count = read(buffer, requested);
        if (count <= 0 || size_t(count) > requested) return false;
        if (!write(buffer, size_t(count))) return false;
        offset += count;
        progress(offset, size);
    }
    return true;
}
