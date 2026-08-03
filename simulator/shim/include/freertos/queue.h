#pragma once
// Simulator shim: queue API lives with the semaphore shim (the firmware only
// peeks mutex state).
#include "freertos/semphr.h"
