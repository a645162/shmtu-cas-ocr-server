# While FetchContent only requires CMake 3.11, selecting C++20 through
# CMAKE_CXX_STANDARD requires 3.12 and the convenience function
# `FetchContent_MakeAvailable` was introduced in CMake 3.14
cmake_minimum_required(VERSION 3.14)
message(STATUS "CMake version: ${CMAKE_VERSION}")

if (NOT DEFINED CMAKE_BUILD_TYPE)
    set(
            CMAKE_BUILD_TYPE Release
            CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE
    )
endif ()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(
            CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install"
            CACHE PATH "Install path prefix, prepended onto install directories." FORCE
    )
endif ()
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

project(SHMTU_CAS_Server VERSION 1.0.0)

#include(FetchContent)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS ON)

message(STATUS "SHMTU_CAS_Server version: ${PROJECT_VERSION}")
add_definitions(-DSHMTU_CAS_SERVER_VERSION="${PROJECT_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ standard used: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build shared libraries: ${BUILD_SHARED_LIBS}")

if (WIN32)
    # Windows

    set(OpenCV_DIR "../3rdparty/OpenCV/opencv500-vs2022/install")
    set(Poco_DIR "../3rdparty/Poco/poco-poco-1.13.2-build/install/cmake")
    set(fmt_DIR "../3rdparty/fmt/fmt-10.2.1-build/install/lib/cmake/fmt")

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Debug Mode")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MDd")
        set(ncnn_DIR "../3rdparty/NCNN/ncnn-vs2022-debug/install/lib/cmake/ncnn")
    else ()
        message(STATUS "Release Mode")
        add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD")

        set(ncnn_DIR "../3rdparty/NCNN/ncnn-vs2022-release/install/lib/cmake/ncnn")
    endif ()

    message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
else ()
    if (CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(LINUX ON)

        execute_process(COMMAND pacman --version RESULT_VARIABLE PACMAN_RESULT OUTPUT_QUIET ERROR_QUIET)

        if (PACMAN_RESULT EQUAL 0)
            message("pacman is available on this system.")
            # Do something specific when pacman is available
            set(PACMAN_AVAILABLE ON)
        else ()
            message("pacman is not available on this system.")
            # Do something specific when pacman is not available
            set(PACMAN_AVAILABLE OFF)
        endif ()
    endif ()

    if (LINUX)
        # Linux

        if (NOT PACMAN_AVAILABLE)
            set(ncnn_DIR "./3rdparty/NCNN/ncnn-20240102-ubuntu-2204/lib/cmake/ncnn")
        endif ()

    endif ()
endif ()
# macOS can install via HomeBrew

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
include_directories(${OpenCV_INCLUDE_DIRS})

find_package(Poco REQUIRED COMPONENTS Foundation Net)

message(STATUS "ncnn_DIR: ${ncnn_DIR}")
find_package(ncnn REQUIRED)

#FetchContent_Declare(fmt
#        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#        GIT_TAG master
#)
#FetchContent_MakeAvailable(fmt)

find_package(fmt REQUIRED)

include_directories(
        ${PROJECT_SOURCE_DIR}/../3rdparty/Tclap/tclap-1.4.0-rc1/include
)

set(SOURCES
        main.cpp

        CAS_OCR.cpp
        CAS_OCR.h
)

# Set Icon
if (WIN32)
    set(WINDOWS_ICON_RC_FILE "app_win32.rc")
endif ()

add_executable(SHMTU_CAS_Server
        ${SOURCES}

        # Windows Icon
        ${WINDOWS_ICON_RC_FILE}
)

if (WIN32)
    target_compile_options(SHMTU_CAS_Server PRIVATE /wd4267)
endif ()

target_link_libraries(SHMTU_CAS_Server PRIVATE ${OpenCV_LIBS})
target_link_libraries(SHMTU_CAS_Server PRIVATE ${Poco_LIBRARIES})
target_link_libraries(SHMTU_CAS_Server PRIVATE ncnn)

if (LINUX)
    target_link_libraries(SHMTU_CAS_Server PRIVATE fmt)
else ()
    target_link_libraries(SHMTU_CAS_Server PRIVATE fmt::fmt)
endif ()
