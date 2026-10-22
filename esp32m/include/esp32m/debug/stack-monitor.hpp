#pragma once

#include <sdkconfig.h>

namespace esp32m::debug {

#if CONFIG_ESP32M_STACK_MONITOR
  /**
   * Sample all tasks' lifetime stack low-water marks when the configured
   * interval elapses. Called automatically from the m/app loop; this does not
   * create another task. Call only from that loop, with scheduling enabled.
   *
   * A snapshot can miss workers created and deleted between samples. Reports
   * identify tasks by number and opaque handle: FreeRTOS task-name pointers
   * cannot safely be dereferenced after a snapshot on a dual-core system.
   */
  void pollStackMonitor();
#else
  inline void pollStackMonitor() {}
#endif

}  // namespace esp32m::debug
