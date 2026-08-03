#pragma once
// Simulator shim: semaphores as depth counters. RenderLock::peek() asks "is
// the rendering mutex held" via xQueuePeek, so mutexes track a hold depth and
// holder rather than being pure no-ops.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct SimSemaphore {
  int depth = 0;
  TaskHandle_t holder = nullptr;
};
using SemaphoreHandle_t = SimSemaphore*;
using QueueHandle_t = SimSemaphore*;

// ESP-IDF's semphr.h transitively provides the queue API; the firmware's only
// queue use is peeking mutex state (RenderLock::peek).
inline BaseType_t xQueuePeek(QueueHandle_t q, void*, TickType_t) {
  // Real semantics: peeking a held mutex fails. Held == depth > 0.
  return (q && q->depth == 0) ? pdTRUE : pdFALSE;
}

SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
  if (!s) return pdFALSE;
  s->depth++;
  s->holder = xTaskGetCurrentTaskHandle();
  return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  if (!s || s->depth == 0) return pdFALSE;
  if (--s->depth == 0) s->holder = nullptr;
  return pdTRUE;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { return xSemaphoreTake(s, t); }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return xSemaphoreGive(s); }
inline TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t s) { return s ? s->holder : nullptr; }
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
