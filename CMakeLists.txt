cmake_minimum_required(VERSION 3.20)
message(STATUS "CMake version: ${CMAKE_VERSION}")

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" SHMTU_CAS_VERSION LIMIT_COUNT 1
    REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT SHMTU_CAS_VERSION)
    message(FATAL_ERROR "Failed to read semantic version from ${CMAKE_SOURCE_DIR}/VERSION")
endif()

# --------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------
project(shmtu-cas-cpp VERSION ${SHMTU_CAS_VERSION} LANGUAGES CXX)

set(SHMTU_CAS_LIB_VERSION    "${PROJECT_VERSION}")
set(SHMTU_CAS_SERVER_VERSION "${PROJECT_VERSION}")
set(SHMTU_CAS_CLI_VERSION    "${PROJECT_VERSION}")
set(SHMTU_CAS_TUI_VERSION    "${PROJECT_VERSION}")
set(SHMTU_CAS_GUI_VERSION    "${PROJECT_VERSION}")
set(SHMTU_CAS_LIB_SOVERSION  "${PROJECT_VERSION_MAJOR}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(ShmtuVersion)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(BUILD_SERVER "Build the HTTP/TCP server executable" ON)
option(BUILD_CLI    "Build the CLI tool executable" ON)
option(BUILD_TUI    "Build the interactive TUI tool" OFF)
option(BUILD_GUI    "Build the GUI application" OFF)
option(USE_VULKAN   "Enable Vulkan/GPU support via ncnn" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
set(SHMTU_NCNN_ROOT "$ENV{SHMTU_NCNN_ROOT}" CACHE PATH "Path to an ncnn installation root or its CMake package directory")

if((SHMTU_NCNN_ROOT OR (DEFINED ENV{NCNN_ROOT} AND NOT "$ENV{NCNN_ROOT}" STREQUAL "")) AND
   DEFINED CMAKE_TOOLCHAIN_FILE AND
   CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
    message(FATAL_ERROR
        "NCNN_ROOT/SHMTU_NCNN_ROOT is set while the vcpkg toolchain is enabled.\n"
        "Use the linux-system* presets when providing a custom ncnn package, for example:\n"
        "  cmake --preset linux-system-vulkan\n"
        "  cmake --build --preset build-linux-system-vulkan")
endif()

# --------------------------------------------------------------------------
# Global C++ standard
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --------------------------------------------------------------------------
# Version definition
# --------------------------------------------------------------------------
message(STATUS "SHMTU_CAS_OCR version: ${PROJECT_VERSION}")
message(STATUS "  lib:    ${SHMTU_CAS_LIB_VERSION}")
message(STATUS "  server: ${SHMTU_CAS_SERVER_VERSION}")
message(STATUS "  cli:    ${SHMTU_CAS_CLI_VERSION}")
message(STATUS "  tui:    ${SHMTU_CAS_TUI_VERSION}")
message(STATUS "  gui:    ${SHMTU_CAS_GUI_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "Build server: ${BUILD_SERVER}")
message(STATUS "Build CLI:    ${BUILD_CLI}")
message(STATUS "Build TUI:    ${BUILD_TUI}")
message(STATUS "Build GUI:    ${BUILD_GUI}")
message(STATUS "Use Vulkan:   ${USE_VULKAN}")

set(SHMTU_CAN_BUILD_GUI FALSE)
if(BUILD_GUI AND EXISTS "${CMAKE_SOURCE_DIR}/ocr/shmtu-cas-ocr-gui/CMakeLists.txt")
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6Widgets_FOUND)
        set(SHMTU_CAN_BUILD_GUI TRUE)
        message(STATUS "Qt6 Widgets found: GUI target enabled")
    else()
        message(STATUS "Qt6 Widgets not found: skipping ocr/shmtu-cas-ocr-gui")
    endif()
endif()

# --------------------------------------------------------------------------
# 3rdparty hint paths
# --------------------------------------------------------------------------
if(WIN32)
    set(OpenCV_DIR       "${CMAKE_SOURCE_DIR}/3rdparty/OpenCV/opencv500-vs2022/install")
    set(Poco_DIR         "${CMAKE_SOURCE_DIR}/3rdparty/Poco/poco-poco-1.13.2-build/install/cmake")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ncnn_DIR     "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-vs2022-debug/install/lib/cmake/ncnn")
    else()
        add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
        set(ncnn_DIR     "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-vs2022-release/install/lib/cmake/ncnn")
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if(SHMTU_NCNN_ROOT)
        set(SHMTU_NCNN_CANDIDATE_DIRS
            "${SHMTU_NCNN_ROOT}"
            "${SHMTU_NCNN_ROOT}/lib/cmake/ncnn"
            "${SHMTU_NCNN_ROOT}/lib64/cmake/ncnn"
            "${SHMTU_NCNN_ROOT}/share/ncnn"
        )
        foreach(shmtu_ncnn_candidate IN LISTS SHMTU_NCNN_CANDIDATE_DIRS)
            if(EXISTS "${shmtu_ncnn_candidate}/ncnnConfig.cmake" OR
               EXISTS "${shmtu_ncnn_candidate}/ncnn-config.cmake")
                set(ncnn_DIR "${shmtu_ncnn_candidate}")
                message(STATUS "Using ncnn package config from SHMTU_NCNN_ROOT: ${ncnn_DIR}")
                break()
            endif()
        endforeach()
    elseif(DEFINED ENV{NCNN_ROOT} AND NOT "$ENV{NCNN_ROOT}" STREQUAL "")
        set(SHMTU_NCNN_CANDIDATE_DIRS
            "$ENV{NCNN_ROOT}"
            "$ENV{NCNN_ROOT}/lib/cmake/ncnn"
            "$ENV{NCNN_ROOT}/lib64/cmake/ncnn"
            "$ENV{NCNN_ROOT}/share/ncnn"
        )
        foreach(shmtu_ncnn_candidate IN LISTS SHMTU_NCNN_CANDIDATE_DIRS)
            if(EXISTS "${shmtu_ncnn_candidate}/ncnnConfig.cmake" OR
               EXISTS "${shmtu_ncnn_candidate}/ncnn-config.cmake")
                set(ncnn_DIR "${shmtu_ncnn_candidate}")
                message(STATUS "Using ncnn package config from NCNN_ROOT: ${ncnn_DIR}")
                break()
            endif()
        endforeach()
    endif()

    execute_process(
        COMMAND pacman --version
        RESULT_VARIABLE PACMAN_RESULT
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT PACMAN_RESULT EQUAL 0 AND NOT ncnn_DIR)
        file(GLOB SHMTU_LOCAL_NCNN_CMAKE_DIRS
            LIST_DIRECTORIES true
            "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-*-ubuntu-*/lib/cmake/ncnn"
        )
        if(SHMTU_LOCAL_NCNN_CMAKE_DIRS)
            list(SORT SHMTU_LOCAL_NCNN_CMAKE_DIRS ORDER DESCENDING)
            list(GET SHMTU_LOCAL_NCNN_CMAKE_DIRS 0 ncnn_DIR)
            message(STATUS "Using bundled ncnn package config from ${ncnn_DIR}")
        endif()
    endif()

    # Ubuntu's Drogon package may ship a legacy FindMySQL.cmake that misses
    # /usr/lib/x86_64-linux-gnu/libmysqlclient.so. Preseed the standard paths
    # for non-vcpkg builds so find_package(Drogon) can resolve its dependency.
    if(NOT (DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$"))
        if(NOT MYSQL_INCLUDE_DIRS AND EXISTS "/usr/include/mysql/mysql.h")
            set(MYSQL_INCLUDE_DIRS "/usr/include/mysql" CACHE PATH "MySQL include directory for Drogon system package" FORCE)
        endif()
        if(NOT MYSQL_LIBRARIES AND EXISTS "/usr/lib/x86_64-linux-gnu/libmysqlclient.so")
            set(MYSQL_LIBRARIES "/usr/lib/x86_64-linux-gnu/libmysqlclient.so" CACHE FILEPATH "MySQL client library for Drogon system package" FORCE)
        endif()
        if(NOT BROTLI_INCLUDE_DIR AND EXISTS "/usr/include/brotli/decode.h")
            set(BROTLI_INCLUDE_DIR "/usr/include" CACHE PATH "Brotli include directory for Drogon system package" FORCE)
        endif()
        if(NOT BROTLIDEC_LIBRARY AND EXISTS "/usr/lib/x86_64-linux-gnu/libbrotlidec.so")
            set(BROTLIDEC_LIBRARY "/usr/lib/x86_64-linux-gnu/libbrotlidec.so" CACHE FILEPATH "Brotli decoder library for Drogon system package" FORCE)
        endif()
        if(NOT BROTLIENC_LIBRARY AND EXISTS "/usr/lib/x86_64-linux-gnu/libbrotlienc.so")
            set(BROTLIENC_LIBRARY "/usr/lib/x86_64-linux-gnu/libbrotlienc.so" CACHE FILEPATH "Brotli encoder library for Drogon system package" FORCE)
        endif()
        if(NOT BROTLICOMMON_LIBRARY AND EXISTS "/usr/lib/x86_64-linux-gnu/libbrotlicommon.so")
            set(BROTLICOMMON_LIBRARY "/usr/lib/x86_64-linux-gnu/libbrotlicommon.so" CACHE FILEPATH "Brotli common library for Drogon system package" FORCE)
        endif()
    endif()
endif()

# --------------------------------------------------------------------------
# Sub-projects
# --------------------------------------------------------------------------
add_subdirectory(ocr/shmtu-cas-ocr-lib)

if(BUILD_SERVER)
    add_subdirectory(ocr/shmtu-cas-ocr-server)
endif()

if(BUILD_CLI)
    add_subdirectory(ocr/shmtu-cas-ocr-cli)
endif()

set(SHMTU_CAN_BUILD_TUI FALSE)
if(BUILD_TUI AND EXISTS "${CMAKE_SOURCE_DIR}/ocr/shmtu-cas-ocr-tui/CMakeLists.txt")
    find_package(ftxui CONFIG QUIET)
    if(ftxui_FOUND)
        set(SHMTU_CAN_BUILD_TUI TRUE)
        message(STATUS "ftxui found: TUI target enabled")
    else()
        message(STATUS "ftxui not found: skipping ocr/shmtu-cas-ocr-tui")
    endif()
endif()

if(SHMTU_CAN_BUILD_TUI)
    add_subdirectory(ocr/shmtu-cas-ocr-tui)
endif()

if(SHMTU_CAN_BUILD_GUI)
    add_subdirectory(ocr/shmtu-cas-ocr-gui)
endif()

foreach(shmtu_ocr_target
    shmtu_cas_ocr_lib
    shmtu_cas_ocr_server
    shmtu_cas_ocr_cli
    shmtu_cas_ocr_tui
    shmtu_cas_ocr_gui
)
    if(TARGET ${shmtu_ocr_target})
        set_property(TARGET ${shmtu_ocr_target} PROPERTY FOLDER "ocr")
    endif()
endforeach()
