cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-gui VERSION ${PROJECT_VERSION} LANGUAGES CXX)

find_package(wxWidgets REQUIRED COMPONENTS core base adv)
include(${wxWidgets_USE_FILE})

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

add_executable(shmtu_cas_ocr_gui
    src/main.cpp
)

target_link_libraries(shmtu_cas_ocr_gui
    PRIVATE
        SHMTU::CasOcrLib
        ${wxWidgets_LIBRARIES}
        Threads::Threads
        CURL::libcurl
)

target_compile_features(shmtu_cas_ocr_gui PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(shmtu_cas_ocr_gui PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
