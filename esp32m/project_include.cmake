# ESP-IDF loads component project hooks before registering any component, so
# these language-specific build properties also reach SDK/managed components.
include("${CMAKE_CURRENT_LIST_DIR}/scripts/stack-analysis.cmake")
esp32m_enable_stack_report("${CMAKE_CURRENT_LIST_DIR}")
