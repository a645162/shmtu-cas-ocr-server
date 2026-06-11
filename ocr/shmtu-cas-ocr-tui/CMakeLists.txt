cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-tui VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(glog QUIET)
if(NOT glog_FOUND)
    find_package(GLOG QUIET)
endif()

# ftxui (provided by vcpkg as a CONFIG package; the targets are
# ftxui::screen, ftxui::dom, ftxui::component).
find_package(ftxui CONFIG QUIET)
if(NOT ftxui_FOUND)
    # Fall back to CMake's find_package which may work with system installs.
    find_package(ftxui CONFIG REQUIRED)
endif()

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
set(TUI_SOURCES
    src/main.cpp
    src/app.cpp
    src/tui/components.cpp
    src/screens/tag_list.cpp
    src/screens/model_list.cpp
    src/screens/download_progress.cpp
    src/screens/download_dialog.cpp
    src/http/http_client.cpp
    src/github/github_client.cpp
    src/github/semver.cpp
)

# --------------------------------------------------------------------------
# Executable target
# --------------------------------------------------------------------------
add_executable(shmtu_cas_ocr_tui ${TUI_SOURCES})

shmtu_apply_target_version(
    shmtu_cas_ocr_tui
    "${SHMTU_CAS_TUI_VERSION}"
)

target_include_directories(shmtu_cas_ocr_tui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(shmtu_cas_ocr_tui
    PRIVATE
        SHMTU::CasOcrLib
        ftxui::screen
        ftxui::dom
        ftxui::component
        CURL::libcurl
        Threads::Threads
)

if(glog_FOUND)
    target_link_libraries(shmtu_cas_ocr_tui PRIVATE glog::glog)
elseif(GLOG_FOUND)
    target_link_libraries(shmtu_cas_ocr_tui PRIVATE ${GLOG_LIBRARIES})
    target_include_directories(shmtu_cas_ocr_tui PRIVATE ${GLOG_INCLUDE_DIRS})
endif()

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(shmtu_cas_ocr_tui PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_tui PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_tui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
