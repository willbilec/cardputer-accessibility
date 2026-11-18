#pragma once
#include <cstdint>
struct ProgramRecord { const char *id, *label, *path; unsigned size; const uint8_t *sha; };
