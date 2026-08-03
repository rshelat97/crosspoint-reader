// Runtime definitions for the simulator's Arduino/FreeRTOS shim layer.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#include "Arduino.h"
#include "HardwareSerial.h"
#include "SPI.h"
#include "WiFi.h"
#include "Wire.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// --- time --------------------------------------------------------------------
namespace {
const auto simEpoch = std::chrono::steady_clock::now();
}

unsigned long millis() {
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - simEpoch).count());
}
unsigned long micros() {
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - simEpoch).count());
}

void delay(unsigned long ms) {
#ifdef __EMSCRIPTEN__
  // Single-threaded browser build: blocking would stall the page. The main
  // loop cadence is driven by the browser instead, so waits are elided.
  (void)ms;
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}
void delayMicroseconds(unsigned int us) {
#ifdef __EMSCRIPTEN__
  (void)us;
#else
  std::this_thread::sleep_for(std::chrono::microseconds(us));
#endif
}
void yield() {}

long random(long howbig) {
  if (howbig <= 0) return 0;
  static std::mt19937 rng(0xC0FFEE);  // deterministic on purpose (reproducible sim runs)
  return static_cast<long>(rng() % static_cast<unsigned long>(howbig));
}

// --- heap statistics ---------------------------------------------------------
// Plausible ESP32-C3 numbers: several code paths (image decode, chapter
// builds) gate on these, and huge host values would exercise unrealistic
// branches.
uint32_t EspClass::getFreeHeap() { return 180 * 1024; }
uint32_t EspClass::getHeapSize() { return 320 * 1024; }
uint32_t EspClass::getMinFreeHeap() { return 96 * 1024; }
uint32_t EspClass::getMaxAllocHeap() { return 110 * 1024; }
[[noreturn]] void EspClass::restart() {
  fprintf(stderr, "[SIM] ESP.restart() requested — exiting simulator\n");
  std::exit(0);
}
EspClass ESP;

[[noreturn]] void esp_restart() { ESP.restart(); }

// --- global peripheral objects ----------------------------------------------
HardwareSerial Serial;
WiFiClass WiFi;
TwoWire Wire;
SPIClass SPI;

// --- FreeRTOS: tasks and semaphores ------------------------------------------
namespace {
SimTask simMainTask;  // the one real thread
std::vector<SimTask*> simTasks;
}  // namespace

TaskHandle_t sim_registerTask(TaskFunction_t fn, void* arg) {
  auto* t = new SimTask{fn, arg, 0};
  simTasks.push_back(t);
  return t;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return &simMainTask; }

void vTaskDelay(TickType_t ticks) { delay(ticks); }

SemaphoreHandle_t xSemaphoreCreateMutex() { return new SimSemaphore(); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new SimSemaphore(); }

// --- uzlib checksums ---------------------------------------------------------
// The vendored uzlib drops its checksum sources (the device gets them from
// ROM); standard implementations matching uzlib.h's signatures.
extern "C" uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  const auto* buf = static_cast<const uint8_t*>(data);
  crc = ~crc;
  for (unsigned int i = 0; i < length; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1u) - 1u)));
  }
  return ~crc;
}
extern "C" uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prevSum) {
  const auto* buf = static_cast<const uint8_t*>(data);
  uint32_t s1 = prevSum & 0xFFFF;
  uint32_t s2 = (prevSum >> 16) & 0xFFFF;
  for (unsigned int i = 0; i < length; i++) {
    s1 = (s1 + buf[i]) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  return (s2 << 16) | s1;
}
