# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require the selected C++ standard")
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Disable compiler-specific C++ extensions")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS "web-ui")

add_compile_definitions(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y)

project(basic)

if(COMMAND esp32m_versioning_add_post_build_bump)
	esp32m_versioning_add_post_build_bump()
endif()
