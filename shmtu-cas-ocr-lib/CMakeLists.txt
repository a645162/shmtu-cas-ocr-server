cmake_minimum_required(VERSION 3.20)

project(shmtu-cas-ocr-lib VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(ncnn REQUIRED)

# --------------------------------------------------------------------------
# Sources — core OCR (always compiled)
# --------------------------------------------------------------------------
set(LIB_SOURCES
    src/cas_ocr.cpp
)

set(LIB_HEADERS_PUBLIC
    include/shmtu/cas_ocr/cas_ocr.h
    include/shmtu/cas_ocr/types.h
)

# --------------------------------------------------------------------------
# Static library target
# --------------------------------------------------------------------------
add_library(shmtu_cas_ocr_lib STATIC ${LIB_SOURCES} ${LIB_HEADERS_PUBLIC})

add_library(SHMTU::CasOcrLib ALIAS shmtu_cas_ocr_lib)

target_include_directories(shmtu_cas_ocr_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(shmtu_cas_ocr_lib
    PUBLIC
        ${OpenCV_LIBS}
        ncnn
)

target_compile_definitions(shmtu_cas_ocr_lib
    PUBLIC
        $<$<BOOL:${USE_VULKAN}>:NCNN_SUPPORT_VULKAN>
)

target_compile_features(shmtu_cas_ocr_lib PUBLIC cxx_std_20)

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(shmtu_cas_ocr_lib PRIVATE /W4 /wd4267)
else()
    target_compile_options(shmtu_cas_ocr_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Install rules
# --------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS shmtu_cas_ocr_lib
    EXPORT shmtu-cas-ocr-lib-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/shmtu
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT shmtu-cas-ocr-lib-targets
    FILE shmtu-cas-ocr-lib-targets.cmake
    NAMESPACE SHMTU::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shmtu-cas-ocr-lib
)
