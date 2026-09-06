#pragma once
#include "FreeRTOS.h"
uint32_t ulTaskNotifyTake(int, uint32_t);
void xTaskNotifyGive(TaskHandle_t);
inline void vTaskDelay(uint32_t) {}
inline int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned,
                       TaskHandle_t *handle)
{
    static int task;
    *handle = &task;
    return pdPASS;
}
