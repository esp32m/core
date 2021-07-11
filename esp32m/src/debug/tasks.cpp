#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/debug/tasks.hpp"

namespace esp32m {
  namespace debug {

    Tasks &Tasks::instance() {
      static Tasks i;
      return i;
    }

    DynamicJsonDocument *Tasks::getState(const JsonVariantConst args) {
      volatile UBaseType_t uxArraySize;
      uxArraySize = uxTaskGetNumberOfTasks();
      DynamicJsonDocument *doc = new DynamicJsonDocument(
          JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(uxArraySize) +
          uxArraySize * (JSON_ARRAY_SIZE(7) + 16));
      auto root = doc->to<JsonObject>();
      auto tasks = root.createNestedArray("tasks");
      TaskStatus_t *pxTaskStatusArray =
          (TaskStatus_t *)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
      if (pxTaskStatusArray) {
        uint32_t ulTotalRunTime;
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize,
                                           &ulTotalRunTime);
        root["rt"] = ulTotalRunTime;
        TaskStatus_t *tp = pxTaskStatusArray;
        for (int i = 0; i < uxArraySize; i++) {
          auto ti = tasks.createNestedArray();
          ti.add(tp->xTaskNumber);
          ti.add((char *)tp->pcTaskName);
          ti.add(tp->eCurrentState);
          ti.add(tp->uxCurrentPriority);
          ti.add(tp->uxBasePriority);
          ti.add(tp->ulRunTimeCounter);
          ti.add(tp->usStackHighWaterMark);
          tp++;
        }
      }
      vPortFree(pxTaskStatusArray);
      return doc;
    }

    Tasks *useTasks() {
      return &Tasks::instance();
    }

  }  // namespace debug
}  // namespace esp32m