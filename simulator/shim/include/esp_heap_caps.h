#pragma once
// Simulator shim: ESP-IDF capability heap. The host has one heap (no PSRAM),
// so every capability maps to malloc; statistics report plausible ESP32-C3
// internal-RAM numbers (matching EspClass in shim.cpp) and zero PSRAM, so
// the firmware takes its no-PSRAM code paths exactly as on an X4.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

inline void* heap_caps_malloc(size_t size, uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? nullptr : malloc(size);
}
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? nullptr : calloc(n, size);
}
inline void* heap_caps_realloc(void* p, size_t size, uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? nullptr : realloc(p, size);
}
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 0 : 180 * 1024; }
inline size_t heap_caps_get_total_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 0 : 320 * 1024; }
inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 0 : 96 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 0 : 110 * 1024; }
