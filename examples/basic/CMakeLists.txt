# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 17)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "../../esp32m" "web-ui")
# OTA over plain HTTP does not work without this as of ESP-IDF 4.4.0
add_compile_definitions(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y)

project(basic VERSION 1)

