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
option(TINYEMAIL_BUILD_EXAMPLES "Build the desktop examples (Send, Receive)" ${_tinyemail_top_level})
option(TINYEMAIL_BUILD_GUI "Build the MailGUI example in an SDL2 window (needs SDL2)" ${_tinyemail_top_level})

# TLS (WiFiClientSecure) is used by the integration test against a TLS server
# and by the examples. OFF avoids building wolfSSL / TinyTLS.
if(TINYEMAIL_BUILD_EXAMPLES)
  set(_tinyemail_tls_default ON)
else()
  set(_tinyemail_tls_default OFF)
endif()
option(TINYEMAIL_TLS "Build the emulator with TLS (WiFiClientSecure)" ${_tinyemail_tls_default})

# ---- Arduino-Emulator: the Arduino core API (Client, Stream, Print) on the desktop
# Fetched from GitHub; override with
# -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=/path/to/checkout.
if(TINYEMAIL_BUILD_TESTS OR TINYEMAIL_BUILD_EXAMPLES)
  set(USE_HTTPS ${TINYEMAIL_TLS} CACHE BOOL "" FORCE)
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

# ---- examples ---------------------------------------------------------------
# MailGUI uses TinyMaterialDesign (TinyGPU, SDL2) on the desktop.
if(TINYEMAIL_BUILD_GUI)
  find_package(SDL2 REQUIRED)
  set(TMD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(TINYEMAIL_TMD_DIR "" CACHE PATH
      "Local TinyMaterialDesign checkout. Default: ../TinyMaterialDesign if present, otherwise fetched from GitHub")
  if(NOT TINYEMAIL_TMD_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TinyMaterialDesign/CMakeLists.txt")
    set(TINYEMAIL_TMD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TinyMaterialDesign" CACHE PATH "" FORCE)
  endif()
  if(TINYEMAIL_TMD_DIR)
    add_subdirectory("${TINYEMAIL_TMD_DIR}" "${CMAKE_BINARY_DIR}/TinyMaterialDesign")
  else()
    FetchContent_Declare(tinymaterialdesign
      GIT_REPOSITORY https://github.com/pschatzmann/TinyMaterialDesign.git
      GIT_TAG main)
    FetchContent_MakeAvailable(tinymaterialdesign)
  endif()

  # TinyFonts renders the text of the mail views (MailGUI/HtmlView.h).
  set(TINYEMAIL_TINYFONTS_DIR "" CACHE PATH
      "Local TinyFonts checkout. Default: ../TinyFonts if present, otherwise fetched from GitHub")
  if(NOT TINYEMAIL_TINYFONTS_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TinyFonts/CMakeLists.txt")
    set(TINYEMAIL_TINYFONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TinyFonts" CACHE PATH "" FORCE)
  endif()
  if(TINYEMAIL_TINYFONTS_DIR)
    add_subdirectory("${TINYEMAIL_TINYFONTS_DIR}" "${CMAKE_BINARY_DIR}/TinyFonts")
  else()
    FetchContent_Declare(tinyfonts
      GIT_REPOSITORY https://github.com/pschatzmann/TinyFonts.git
      GIT_TAG main)
    FetchContent_MakeAvailable(tinyfonts)
  endif()

  # TinyFATFS stores the mails (TinyEMail/TinyMailStore.h).
  set(TINYEMAIL_TINYFATFS_DIR "" CACHE PATH
      "Local TinyFATFS checkout. Default: ../TinyFATFS if present, otherwise fetched from GitHub")
  if(NOT TINYEMAIL_TINYFATFS_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TinyFATFS/CMakeLists.txt")
    set(TINYEMAIL_TINYFATFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TinyFATFS" CACHE PATH "" FORCE)
  endif()
  set(FATFS_EXAMPES OFF CACHE BOOL "" FORCE)
  set(FATFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FATFS_SANITIZE OFF CACHE BOOL "" FORCE)
  if(TINYEMAIL_TINYFATFS_DIR)
    add_subdirectory("${TINYEMAIL_TINYFATFS_DIR}" "${CMAKE_BINARY_DIR}/TinyFATFS")
  else()
    FetchContent_Declare(tinyfatfs
      GIT_REPOSITORY https://github.com/pschatzmann/TinyFATFS.git
      GIT_TAG main)
    FetchContent_MakeAvailable(tinyfatfs)
  endif()

  # TinyJPEG decodes the JPEG pictures of the attachments (via TinyGPU's JPEGParser).
  set(TINYEMAIL_TINYJPEG_DIR "" CACHE PATH
      "Local TinyJPEG checkout. Default: ../TinyJPEG if present, otherwise fetched from GitHub")
  if(NOT TINYEMAIL_TINYJPEG_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TinyJPEG/CMakeLists.txt")
    set(TINYEMAIL_TINYJPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TinyJPEG" CACHE PATH "" FORCE)
  endif()
  set(TINYJPEG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  if(TINYEMAIL_TINYJPEG_DIR)
    add_subdirectory("${TINYEMAIL_TINYJPEG_DIR}" "${CMAKE_BINARY_DIR}/TinyJPEG")
  else()
    FetchContent_Declare(tinyjpeg
      GIT_REPOSITORY https://github.com/pschatzmann/TinyJPEG.git
      GIT_TAG main)
    FetchContent_MakeAvailable(tinyjpeg)
  endif()
endif()

if(TINYEMAIL_BUILD_EXAMPLES OR TINYEMAIL_BUILD_GUI)
  add_subdirectory(examples)
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
