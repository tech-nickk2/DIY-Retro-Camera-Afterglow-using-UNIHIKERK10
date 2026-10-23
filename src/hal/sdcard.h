// microSD on SPI3, mounted as FAT at /sd with the ESP-IDF VFS, so plain
// fopen/fwrite work. The font ROM on the same bus is deselected whenever the
// card is selected (their chip selects are inverted copies of GPIO40).
#pragma once
#include <stdint.h>

namespace sdcard {

constexpr const char* ROOT = "/sd";

bool begin();
bool mounted();
// Sizes in MB; both 0 when no card is mounted.
void usage(uint32_t* totalMB, uint32_t* freeMB);
// Makes a directory under /sd if it is missing.
bool ensureDir(const char* path);
// Next free /sd/DCIM/<prefix>NNNN.<ext> name; returns false if there is no card.
bool nextName(const char* prefix, const char* ext, char* out, int outLen);

}  // namespace sdcard
