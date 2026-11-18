#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace playpalace {
enum class Direction { Up, Down, Left, Right };

inline int navigate(int selected, int count, int start, int width,
                    const std::vector<int>& rows, Direction direction, bool edge) {
    if (count == 0) return 0;
    selected = std::clamp(selected, 0, count - 1);
    start = std::clamp(start, 0, count);
    const bool vertical = direction == Direction::Up || direction == Direction::Down;
    const bool forward = direction == Direction::Right || direction == Direction::Down;
    if (selected < start) {
        if (!vertical || edge) return selected;
        return std::clamp(selected + (forward ? 1 : -1), 0, count - 1);
    }
    std::vector<int> boundaries{start};
    const bool ragged = !rows.empty() && std::all_of(rows.begin(), rows.end(), [count](int n) {
        return n > 0 && n <= count;
    }) && std::accumulate(rows.begin(), rows.end(), int64_t{0}) == count - start;
    if (ragged) {
        for (int length : rows) boundaries.push_back(boundaries.back() + length);
    } else {
        width = std::max(width, 1);
        while (boundaries.back() < count)
            boundaries.push_back(boundaries.back() + std::min(width, count - boundaries.back()));
    }
    const int row = int(std::upper_bound(boundaries.begin(), boundaries.end(), selected)
                        - boundaries.begin()) - 1;
    const int last = int(boundaries.size()) - 2;
    if (!vertical) {
        return edge ? (forward ? boundaries[row + 1] - 1 : boundaries[row])
                    : std::clamp(selected + (forward ? 1 : -1), boundaries[row], boundaries[row + 1] - 1);
    }
    if (!ragged && !edge) {
        if (!forward && row == 0 && start > 0) return start - 1;
        if (forward && selected + width >= count) return selected;
    }
    const int target = edge ? (forward ? last : 0) : std::clamp(row + (forward ? 1 : -1), 0, last);
    const int column = selected - boundaries[row];
    if (!ragged && edge && forward && boundaries[target] + column >= count)
        return boundaries[target - 1] + column;
    return std::min(boundaries[target] + column, boundaries[target + 1] - 1);
}
}
