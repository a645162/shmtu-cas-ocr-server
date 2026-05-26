cmake_minimum_required(VERSION 3.20)
message(STATUS "CMake version: ${CMAKE_VERSION}")

# --------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------
project(SHMTU_CAS_OCR VERSION 1.0.0 LANGUAGES CXX)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(BUILD_SERVER "Build the HTTP/TCP server executable" ON)
option(BUILD_CLI    "Build the CLI tool executable" ON)
option(USE_VULKAN   "Enable Vulkan/GPU support via ncnn" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# --------------------------------------------------------------------------
# Conan integration (CMake dependency provider)
# --------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/conan_provider.cmake")
    include("${CMAKE_SOURCE_DIR}/conan_provider.cmake")
endif()

# --------------------------------------------------------------------------
# Global C++ standard
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------------
# Version definition
# --------------------------------------------------------------------------
add_definitions(-DSHMTU_CAS_SERVER_VERSION="${PROJECT_VERSION}")

message(STATUS "SHMTU_CAS_OCR version: ${PROJECT_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "Build server: ${BUILD_SERVER}")
message(STATUS "Build CLI:    ${BUILD_CLI}")
message(STATUS "Use Vulkan:   ${USE_VULKAN}")

# --------------------------------------------------------------------------
# 3rdparty hint paths (used when Conan is not active)
# --------------------------------------------------------------------------
if(WIN32)
    set(OpenCV_DIR       "${CMAKE_SOURCE_DIR}/3rdparty/OpenCV/opencv500-vs2022/install")
    set(Poco_DIR         "${CMAKE_SOURCE_DIR}/3rdparty/Poco/poco-poco-1.13.2-build/install/cmake")
    set(fmt_DIR          "${CMAKE_SOURCE_DIR}/3rdparty/fmt/fmt-10.2.1-build/install/lib/cmake/fmt")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ncnn_DIR     "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-vs2022-debug/install/lib/cmake/ncnn")
    else()
        add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
        set(ncnn_DIR     "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-vs2022-release/install/lib/cmake/ncnn")
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    execute_process(
        COMMAND pacman --version
        RESULT_VARIABLE PACMAN_RESULT
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT PACMAN_RESULT EQUAL 0)
        set(ncnn_DIR "${CMAKE_SOURCE_DIR}/3rdparty/NCNN/ncnn-20240102-ubuntu-2204/lib/cmake/ncnn")
    endif()
endif()

# --------------------------------------------------------------------------
# FetchContent — cpp-httplib (header-only, used by server)
# --------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
)

# Only fetch if we are building the server
if(BUILD_SERVER)
    FetchContent_MakeAvailable(cpp-httplib)
endif()

# --------------------------------------------------------------------------
# Sub-projects
# --------------------------------------------------------------------------
add_subdirectory(shmtu-cas-ocr-lib)

if(BUILD_SERVER)
    add_subdirectory(shmtu-cas-ocr-server)
endif()

if(BUILD_CLI)
    add_subdirectory(shmtu-cas-ocr-cli)
endif()
