#pragma once
#include <cstdint>
using TaskHandle_t = void *;
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
constexpr int pdPASS = 1;
constexpr int pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
inline void taskENTER_CRITICAL(portMUX_TYPE *) {}
inline void taskEXIT_CRITICAL(portMUX_TYPE *) {}
