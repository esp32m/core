# Bumps a project version stored in version.txt.
# Format: Major.Minor.Build (all integers).
#
# Usage:
#   cmake -DVERSION_FILE=path/to/version.txt -P bump_version.cmake

if(NOT DEFINED VERSION_FILE)
  message(WARNING "Failed to bump build number: VERSION_FILE not set")
  return()
endif()

set(_vf "${VERSION_FILE}")
string(STRIP "${_vf}" _vf)
# Handle accidental quoting like "C:/path/version.txt" or \"C:/path/version.txt\"
string(REGEX REPLACE "^\\\\\"(.*)\\\\\"$" "\\1" _vf "${_vf}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" _vf "${_vf}")
set(VERSION_FILE "${_vf}")

set(_initial "1.0.1")

if(NOT EXISTS "${VERSION_FILE}")
  # First build with no version.txt: create it, but do not increment yet.
  file(WRITE "${VERSION_FILE}" "${_initial}\n")
  message(STATUS "Project version file missing; created ${_initial} (no bump)")
  return()
else()
  file(READ "${VERSION_FILE}" _content)
  string(REPLACE "\r\n" "\n" _content "${_content}")
  string(REPLACE "\r" "\n" _content "${_content}")
  string(REGEX REPLACE "\n.*$" "" _ver "${_content}")
  string(STRIP "${_ver}" _ver)
  if(_ver STREQUAL "")
    # Empty file: initialize, but do not increment yet.
    file(WRITE "${VERSION_FILE}" "${_initial}\n")
    message(STATUS "Project version file empty; initialized to ${_initial} (no bump)")
    return()
  endif()
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _ok "${_ver}")
if(NOT _ok)
  message(WARNING "Failed to bump build number: invalid version '${_ver}' in ${VERSION_FILE} (expected Major.Minor.Build, e.g. 1.0.1)")
  return()
endif()

set(_major "${CMAKE_MATCH_1}")
set(_minor "${CMAKE_MATCH_2}")
set(_build "${CMAKE_MATCH_3}")

math(EXPR _build "${_build} + 1")
set(_next "${_major}.${_minor}.${_build}")

file(WRITE "${VERSION_FILE}" "${_next}\n")
message(STATUS "Current project version ${_ver}, next version bumped to ${_next}")
