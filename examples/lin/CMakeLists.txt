cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require the selected C++ standard")
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Disable compiler-specific C++ extensions")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS "web-ui")

add_compile_definitions(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y)

# set node role:  1 = LIN master,  2 = LIN slave
add_compile_definitions(LIN_NODE_NUM=1)

project(lin)

if(COMMAND esp32m_versioning_add_post_build_bump)
	esp32m_versioning_add_post_build_bump()
endif()
