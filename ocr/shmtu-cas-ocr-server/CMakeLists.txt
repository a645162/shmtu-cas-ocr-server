cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-server VERSION ${PROJECT_VERSION} LANGUAGES CXX)

find_package(CLI11 CONFIG QUIET)
if(NOT CLI11_FOUND)
    find_package(CLI11 REQUIRED)
endif()
find_package(Drogon REQUIRED CONFIG)
find_package(Trantor REQUIRED CONFIG)
find_package(glog CONFIG QUIET)
if(NOT glog_FOUND)
    find_package(glog REQUIRED)
endif()

set(SERVER_SOURCES
    src/logging.cpp
    src/main.cpp
    src/server_config.cpp
    src/server_startup.cpp
    src/server.cpp
    src/server_http.cpp
    src/server_tcp.cpp
    src/server_worker_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shmtu-cas-ocr-gui/src/model_download.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../shmtu-cas-ocr-gui/src/logging.cpp
)

add_executable(shmtu_cas_ocr_server ${SERVER_SOURCES})

shmtu_apply_target_version(
    shmtu_cas_ocr_server
    "${SHMTU_CAS_SERVER_VERSION}"
)

target_include_directories(shmtu_cas_ocr_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../shmtu-cas-ocr-gui/include
)

target_link_libraries(shmtu_cas_ocr_server
    PRIVATE
        SHMTU::CasOcrLib
        CLI11::CLI11
        Drogon::Drogon
        Trantor::Trantor
        glog::glog
)

if(MSVC)
    target_compile_options(shmtu_cas_ocr_server PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
