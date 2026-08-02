cmake_minimum_required(VERSION 3.24)

project(FalconPatch
    VERSION 0.1.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_OSSFUZZ OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG v1.11.4
)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
FetchContent_MakeAvailable(libzip)

file(GLOB COMMAND_SOURCES "src/commands/*.c")
file(GLOB UTIL_SOURCES "src/utils/*.c")

add_executable(fpatch
    src/main.c
    src/cli.c
    ${COMMAND_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(fpatch PRIVATE include)
target_link_libraries(fpatch PRIVATE zip)

if(MSVC)
    target_compile_definitions(fpatch PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(fpatch PRIVATE /W4)
else()
    target_compile_options(fpatch PRIVATE -Wall -Wextra -Wpedantic)
endif()
