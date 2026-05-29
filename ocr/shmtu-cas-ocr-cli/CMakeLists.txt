cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-cli VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
find_package(CLI11 CONFIG QUIET)
if(NOT CLI11_FOUND)
    find_package(CLI11 REQUIRED)
endif()

# cpp-httplib requires threading
find_package(Threads REQUIRED)

# Optional: OpenSSL for HTTPS support in cpp-httplib
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found — HTTPS support enabled in CLI HTTP client")
else()
    message(STATUS "OpenSSL not found — HTTPS support disabled in CLI HTTP client")
endif()

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
set(CLI_SOURCES
    src/cli_config.cpp
    src/cli_files.cpp
    src/cli_json.cpp
    src/cli_remote.cpp
    src/cli_runner.cpp
    src/main.cpp
)

# --------------------------------------------------------------------------
# Executable target
# --------------------------------------------------------------------------
add_executable(shmtu_cas_ocr_cli ${CLI_SOURCES})

shmtu_apply_target_version(
    shmtu_cas_ocr_cli
    "${SHMTU_CAS_CLI_VERSION}"
)

target_include_directories(shmtu_cas_ocr_cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/3rdparty/cpp-httplib
)

target_link_libraries(shmtu_cas_ocr_cli
    PRIVATE
        SHMTU::CasOcrLib
        CLI11::CLI11
        Threads::Threads
)

if(OpenSSL_FOUND)
    target_link_libraries(shmtu_cas_ocr_cli PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(shmtu_cas_ocr_cli PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

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
