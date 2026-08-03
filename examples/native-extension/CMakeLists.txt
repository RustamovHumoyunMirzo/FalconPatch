cmake_minimum_required(VERSION 3.22)
project(FalconPatchExampleExtension LANGUAGES C)

if(NOT FALCONPATCH_RUNTIME_LIBRARY)
    message(FATAL_ERROR "Set FALCONPATCH_RUNTIME_LIBRARY to the target ABI libfalconpatch.so")
endif()
if(NOT FALCONPATCH_LUA_INCLUDE_DIR)
    message(FATAL_ERROR "Set FALCONPATCH_LUA_INCLUDE_DIR to the FalconPatch SDK include directory")
endif()

add_library(falconpatch_runtime SHARED IMPORTED)
set_target_properties(falconpatch_runtime PROPERTIES
    IMPORTED_LOCATION "${FALCONPATCH_RUNTIME_LIBRARY}")

add_library(fpatch_example SHARED example_extension.c)
target_include_directories(fpatch_example PRIVATE
    ../../android/include
    "${FALCONPATCH_LUA_INCLUDE_DIR}"
)
target_link_libraries(fpatch_example PRIVATE falconpatch_runtime)
