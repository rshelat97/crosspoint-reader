#pragma once
// Simulator shim: single-threaded FreeRTOS surface. The firmware's only real
// concurrency is the ActivityManager render task; the simulator collapses it
// to synchronous rendering (see CROSSPOINT_SIMULATOR hooks in
// src/activities/ActivityManager.cpp), so mutexes become depth counters and
// task calls become bookkeeping. Everything here is deliberately NOT
// thread-safe — the sim runs on one thread by design.
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
#define tskIDLE_PRIORITY 0
#define configNUM_CORES 1

// Spinlocks: no-ops on a single thread.
struct portMUX_TYPE_t {
  int dummy;
};
using portMUX_TYPE = portMUX_TYPE_t;
#define portMUX_INITIALIZER_UNLOCKED {0}
inline void taskENTER_CRITICAL(portMUX_TYPE*) {}
inline void taskEXIT_CRITICAL(portMUX_TYPE*) {}
