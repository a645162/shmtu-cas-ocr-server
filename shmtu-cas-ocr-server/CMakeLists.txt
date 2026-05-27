cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-server VERSION ${PROJECT_VERSION} LANGUAGES CXX)

find_package(Drogon REQUIRED CONFIG)
find_package(Trantor REQUIRED CONFIG)

set(SERVER_SOURCES
    src/main.cpp
    src/server.cpp
    src/server_http.cpp
    src/server_tcp.cpp
    src/server_worker_pool.cpp
)

add_executable(shmtu_cas_ocr_server ${SERVER_SOURCES})

target_include_directories(shmtu_cas_ocr_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(shmtu_cas_ocr_server
    PRIVATE
        SHMTU::CasOcrLib
        Drogon::Drogon
        Trantor::Trantor
)

target_compile_features(shmtu_cas_ocr_server PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(shmtu_cas_ocr_server PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
