# Comix Zone XBLA - ReXGlue Static Recompilation Project
cmake_minimum_required(VERSION 3.25)
project(comixzone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find ReXGlue SDK
if(DEFINED ENV{REXSDK})
    set(REXSDK_PATH "$ENV{REXSDK}")
else()
    set(REXSDK_PATH "${CMAKE_SOURCE_DIR}/../ctxbla/tools/rexglue-sdk/out/install/win-amd64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${REXSDK_PATH}")

find_package(rexglue REQUIRED)

# Include generated source list
include(${CMAKE_SOURCE_DIR}/generated/sources.cmake)

# Platform entry point from SDK
if(WIN32)
    set(ENTRY_POINT_SRC "${REXSDK_PATH}/share/rexglue/windowed_app_main_win.cpp")
else()
    set(ENTRY_POINT_SRC "${REXSDK_PATH}/share/rexglue/windowed_app_main_posix.cpp")
endif()

# Main executable
if(WIN32)
    add_executable(comixzone WIN32
        src/main.cpp
        src/stubs.cpp
        src/keyboard_driver.cpp
        ${ENTRY_POINT_SRC}
        ${GENERATED_SOURCES}
    )
else()
    add_executable(comixzone
        src/main.cpp
        src/stubs.cpp
        src/keyboard_driver.cpp
        ${ENTRY_POINT_SRC}
        ${GENERATED_SOURCES}
    )
endif()

target_include_directories(comixzone PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/generated
)

target_link_libraries(comixzone PRIVATE
    rex::core
    rex::runtime
    rex::kernel
    rex::graphics
    rex::ui
)

# Compile options matching rexglue requirements
target_compile_options(comixzone PRIVATE
    -fno-strict-aliasing
    -ffp-model=strict
    -fno-char8_t
    $<$<CONFIG:DEBUG>:-g>
    $<$<CONFIG:DEBUG>:-O0>
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
)

if(NOT MSVC)
    target_compile_options(comixzone PRIVATE -msse4.1)
endif()

# Allow stubs.cpp to override SDK kernel function symbols
if(WIN32)
    target_link_options(comixzone PRIVATE "LINKER:/force:multiple")
    target_link_options(comixzone PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:rex::kernel>")
endif()

# Console test executable (no GUI)
add_executable(comixzone_test
    src/test_boot.cpp
    src/stubs.cpp
    ${GENERATED_SOURCES}
)
target_include_directories(comixzone_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/generated
)
target_link_libraries(comixzone_test PRIVATE
    rex::core
    rex::runtime
    rex::kernel
    rex::graphics
    rex::ui
)
target_compile_options(comixzone_test PRIVATE
    -fno-strict-aliasing
    -ffp-model=strict
    -fno-char8_t
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
)
if(NOT MSVC)
    target_compile_options(comixzone_test PRIVATE -msse4.1)
endif()

if(WIN32)
    target_link_options(comixzone_test PRIVATE "LINKER:/force:multiple")
    target_link_options(comixzone_test PRIVATE "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:rex::kernel>")
endif()

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_link_options(comixzone PRIVATE
        -Wl,--whole-archive $<TARGET_FILE:rex::kernel> -Wl,--no-whole-archive
    )
    target_compile_options(comixzone PRIVATE -mcmodel=large)
endif()
