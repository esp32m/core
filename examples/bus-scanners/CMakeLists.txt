# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS "web-ui")

add_compile_definitions(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y)

project(bus-scanners VERSION 1)


