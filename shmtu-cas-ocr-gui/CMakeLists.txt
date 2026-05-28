cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-gui VERSION ${PROJECT_VERSION} LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(shmtu_cas_ocr_gui
    src/main.cpp
    src/logging.cpp
    src/main_window.cpp
    src/model_download.cpp
)

target_include_directories(shmtu_cas_ocr_gui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(shmtu_cas_ocr_gui
    PRIVATE
        SHMTU::CasOcrLib
        Qt6::Widgets
        Threads::Threads
        CURL::libcurl
)

if(MSVC)
    target_compile_options(shmtu_cas_ocr_gui PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
