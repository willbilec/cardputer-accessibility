#pragma once
// Original action IDs retained so the upstream action handlers stay shared.
constexpr int headlessSettings[] = {4, 5, 6, 7, 10, 11, 12, 13, 14, 15};
constexpr int headlessSettingCount = sizeof(headlessSettings) / sizeof(headlessSettings[0]);
inline int headlessSettingPosition(int id) {
    for (int i = 0; i < headlessSettingCount; ++i) if (headlessSettings[i] == id) return i;
    return 0;
}
inline int headlessNextSetting(int id, int direction) {
    return headlessSettings[(headlessSettingPosition(id) + direction + headlessSettingCount) % headlessSettingCount];
}
