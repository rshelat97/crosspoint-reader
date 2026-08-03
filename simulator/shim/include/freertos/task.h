#pragma once
// Simulator shim: FreeRTOS task API. Tasks are registered but never run as
// threads; the simulator drives rendering synchronously. Task notifications
// are per-handle counters so ulTaskNotifyTake semantics hold if anything
// polls them.
#include "freertos/FreeRTOS.h"

struct SimTask {
  void (*fn)(void*) = nullptr;
  void* arg = nullptr;
  uint32_t notifications = 0;
};
using TaskHandle_t = SimTask*;
using TaskFunction_t = void (*)(void*);

enum eNotifyAction { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite };

TaskHandle_t sim_registerTask(TaskFunction_t fn, void* arg);
TaskHandle_t xTaskGetCurrentTaskHandle();

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t,
                                          TaskHandle_t* outHandle, BaseType_t) {
  TaskHandle_t h = sim_registerTask(fn, arg);
  if (outHandle) *outHandle = h;
  return pdPASS;
}
inline BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                              TaskHandle_t* outHandle) {
  return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, outHandle, 0);
}
inline void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks);
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 65535; }

inline BaseType_t xTaskNotify(TaskHandle_t task, uint32_t, eNotifyAction) {
  if (task) task->notifications++;
  return pdTRUE;
}
inline uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t) {
  // Only meaningful for the render task, which the simulator never runs as a
  // real loop; if anything else calls this on the main thread, return the
  // pending count without blocking (single thread: blocking would deadlock).
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  const uint32_t n = self ? self->notifications : 0;
  if (self && clearOnExit == pdTRUE) self->notifications = 0;
  return n;
}
