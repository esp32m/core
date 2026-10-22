#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = void*;
struct TaskStatus_t {
  TaskHandle_t xHandle;
  const char* pcTaskName;
  UBaseType_t xTaskNumber;
  StackType_t* pxStackBase;
  uint32_t usStackHighWaterMark;
};
UBaseType_t uxTaskGetNumberOfTasks();
UBaseType_t uxTaskGetSystemState(TaskStatus_t*, UBaseType_t, uint32_t*);
