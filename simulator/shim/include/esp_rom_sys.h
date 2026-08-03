#pragma once
// Simulator shim: ROM printf goes to stdout.
#include <cstdio>

#define esp_rom_printf printf
