cmake_minimum_required(VERSION 3.16)

project(FalconPatch
    VERSION 0.1.0
    DESCRIPTION "Developer-owned Android module patch workflow starter"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(fpatch src/main.c)

target_include_directories(fpatch PRIVATE include)

if(MSVC)
    target_compile_definitions(fpatch PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(fpatch PRIVATE /W4)
else()
    target_compile_options(fpatch PRIVATE -Wall -Wextra -Wpedantic)
endif()
