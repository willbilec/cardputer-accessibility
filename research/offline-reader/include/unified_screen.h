#pragma once

#ifdef UNIFIED_SCREEN
#include <Arduino.h>

namespace unifiedScreen {
enum class Icon { Menu, Folder, Settings, Radio, Book, Controls };
using MenuLabel = String (*)(int);
void menu(const char *title, Icon icon, int count, int selected, MenuLabel label,
          const char *help = nullptr);
}
#endif
