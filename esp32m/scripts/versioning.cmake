# Shared ESP-IDF project versioning helpers.
#
# Goal:
# - Single source of truth is version.txt in project root
# - Version format is Major.Minor.Build (all integers)
# - PROJECT_VER is set from version.txt before calling project()
# - On successful link, version.txt is bumped to Major.Minor.(Build+1)
#
# Typical usage in a project's top-level CMakeLists.txt:
#   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#   include("${CMAKE_CURRENT_LIST_DIR}/../../core/esp32m/scripts/versioning.cmake")
#   esp32m_versioning_set_project_ver()
#   project(myapp)
#   esp32m_versioning_add_post_build_bump()

include_guard(GLOBAL)

# Capture this module's directory at include() time.
# Note: CMAKE_CURRENT_LIST_DIR inside a function points to the caller's list file,
# which would incorrectly resolve scripts relative to the project.
set(ESP32M_VERSIONING_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

function(_esp32m_versioning_read_first_line path out_var)
  if(EXISTS "${path}")
    file(READ "${path}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")
    string(REPLACE "\r" "\n" _content "${_content}")
    string(REGEX REPLACE "\n.*$" "" _line "${_content}")
    string(STRIP "${_line}" _line)
    set(${out_var} "${_line}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

# Sets PROJECT_VER before calling project().
# Optional args:
#   VERSION_FILE <path>
#   INITIAL <major.minor.build>   (default: 1.0.1)
function(esp32m_versioning_set_project_ver)
  set(options)
  set(oneValueArgs VERSION_FILE INITIAL)
  set(multiValueArgs)
  cmake_parse_arguments(EV "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(EV_VERSION_FILE)
    set(_version_file "${EV_VERSION_FILE}")
  else()
    set(_version_file "${CMAKE_SOURCE_DIR}/version.txt")
  endif()

  if(EV_INITIAL)
    set(_initial "${EV_INITIAL}")
  else()
    set(_initial "1.0.1")
  endif()

  if(NOT EXISTS "${_version_file}")
    file(WRITE "${_version_file}" "${_initial}\n")
  endif()

  _esp32m_versioning_read_first_line("${_version_file}" _ver)
  if(_ver STREQUAL "")
    set(_ver "${_initial}")
    file(WRITE "${_version_file}" "${_ver}\n")
  endif()

  string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _ok "${_ver}")
  if(NOT _ok)
    message(FATAL_ERROR "Invalid version in ${_version_file}: '${_ver}'. Expected Major.Minor.Build (e.g. 1.0.1)")
  endif()

  # Make CMake auto-reconfigure when version.txt changes (so the next build picks up the bumped number)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_version_file}")

  set(PROJECT_VER "${_ver}" PARENT_SCOPE)

  # Store path for the post-build bump helper
  set(ESP32M_VERSIONING_VERSION_FILE "${_version_file}" CACHE INTERNAL "")
endfunction()

# Adds a POST_BUILD step to increment the counter after a successful link.
# Call this after project().
function(esp32m_versioning_add_post_build_bump)
  if(NOT DEFINED ESP32M_VERSIONING_VERSION_FILE OR ESP32M_VERSIONING_VERSION_FILE STREQUAL "")
    set(ESP32M_VERSIONING_VERSION_FILE "${CMAKE_SOURCE_DIR}/version.txt")
  endif()

  # idf_build_get_property is provided by ESP-IDF's build system
  if(NOT COMMAND idf_build_get_property)
    message(WARNING "esp32m_versioning_add_post_build_bump(): ESP-IDF not detected (idf_build_get_property missing); skipping build number bump")
    return()
  endif()

  idf_build_get_property(_project_elf EXECUTABLE)
  if(NOT _project_elf)
    message(WARNING "esp32m_versioning_add_post_build_bump(): could not resolve project ELF target; skipping build number bump")
    return()
  endif()

  if(DEFINED ESP32M_VERSIONING_MODULE_DIR AND NOT ESP32M_VERSIONING_MODULE_DIR STREQUAL "")
    set(_module_dir "${ESP32M_VERSIONING_MODULE_DIR}")
  else()
    # Fallback: best-effort; may resolve to caller.
    set(_module_dir "${CMAKE_CURRENT_LIST_DIR}")
  endif()

  set(_bump_script "${_module_dir}/bump_version.cmake")

  add_custom_command(
    TARGET ${_project_elf}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DVERSION_FILE:FILEPATH=${ESP32M_VERSIONING_VERSION_FILE} -P "${_bump_script}"
    VERBATIM
  )
endfunction()
