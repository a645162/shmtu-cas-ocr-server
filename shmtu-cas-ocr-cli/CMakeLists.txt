cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-cli VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
find_package(fmt REQUIRED)

# tclap is header-only; include from 3rdparty
include_directories(
    ${CMAKE_SOURCE_DIR}/3rdparty/Tclap/tclap-1.4.0-rc1/include
)

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
set(CLI_SOURCES
    src/main.cpp
)

# --------------------------------------------------------------------------
# Executable target
# --------------------------------------------------------------------------
add_executable(shmtu_cas_ocr_cli ${CLI_SOURCES})

target_link_libraries(shmtu_cas_ocr_cli
    PRIVATE
        SHMTU::CasOcrLib
        fmt::fmt
)

target_compile_features(shmtu_cas_ocr_cli PRIVATE cxx_std_20)

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(shmtu_cas_ocr_cli PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
