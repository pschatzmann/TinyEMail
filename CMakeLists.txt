cmake_minimum_required(VERSION 3.14)

project(TinyEMail
  VERSION 0.1.0
  DESCRIPTION "Header-only e-mail client for Gmail with an Arduino style API"
  LANGUAGES CXX)

# Sub-projects (Arduino-Emulator) call project() themselves and overwrite
# PROJECT_VERSION, so keep our own copy.
set(TINYEMAIL_VERSION ${PROJECT_VERSION})

include(GNUInstallDirs)
include(FetchContent)

# ---- header-only target -------------------------------------------------
add_library(TinyEMail INTERFACE)
add_library(TinyEMail::TinyEMail ALIAS TinyEMail)

target_include_directories(TinyEMail INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(TinyEMail INTERFACE cxx_std_11)

# ---- options ----------------------------------------------------------------
set(_tinyemail_top_level OFF)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(_tinyemail_top_level ON)
endif()

option(TINYEMAIL_BUILD_TESTS "Build the desktop unit tests" ${_tinyemail_top_level})
option(TINYEMAIL_BUILD_DOCS "Add the 'docs' target that runs Doxygen" ${_tinyemail_top_level})

# ---- Arduino-Emulator: the Arduino core API (Client, Stream, Print) on the desktop
# Fetched from GitHub; override with
# -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=/path/to/checkout.
if(TINYEMAIL_BUILD_TESTS)
  # TLS (WiFiClientSecure) is only needed by the integration test against a TLS server
  option(TINYEMAIL_TLS "Build the emulator with TLS (WiFiClientSecure)" OFF)
  set(USE_HTTPS ${TINYEMAIL_TLS} CACHE BOOL "" FORCE)  # OFF avoids building wolfSSL / TinyTLS
  # Pinned to the revision the other pschatzmann desktop libraries are tested with.
  FetchContent_Declare(arduino_emulator
    GIT_REPOSITORY https://github.com/pschatzmann/Arduino-Emulator.git
    GIT_TAG 0410982)  # includes the emulator fix that defines USE_HTTPS for linking targets
  # EXCLUDE_FROM_ALL keeps the emulator's install rules out of cmake --install
  FetchContent_GetProperties(arduino_emulator)
  if(NOT arduino_emulator_POPULATED)
    FetchContent_Populate(arduino_emulator)
    add_subdirectory(${arduino_emulator_SOURCE_DIR} ${arduino_emulator_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
endif()

# ---- tests ------------------------------------------------------------------
if(TINYEMAIL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- documentation ------------------------------------------------------------
# 'cmake --build build --target docs' writes HTML to docs/html (Doxyfile).
if(TINYEMAIL_BUILD_DOCS)
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    add_custom_target(docs
      COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM)
  else()
    message(STATUS "Doxygen not found: the 'docs' target is not available")
  endif()
endif()

# ---- installation / package config ---------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS TinyEMail EXPORT TinyEMailTargets)
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
install(EXPORT TinyEMailTargets
        FILE TinyEMailTargets.cmake
        NAMESPACE TinyEMail::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEMail)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyEMailConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/TinyEMailConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEMail)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/TinyEMailConfigVersion.cmake
  VERSION ${TINYEMAIL_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/TinyEMailConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/TinyEMailConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TinyEMail)
