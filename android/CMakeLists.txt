cmake_minimum_required(VERSION 3.22)

project(FalconPatchAndroid VERSION 1.8.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LUA_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/lua")
if(NOT EXISTS "${LUA_SRC_DIR}/lapi.c" OR NOT EXISTS "${LUA_SRC_DIR}/lua.h")
    message(FATAL_ERROR "Vendored Lua source is incomplete: ${LUA_SRC_DIR}")
endif()
set(LUA_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

add_library(fpatch_lua STATIC ${LUA_SOURCES})
target_include_directories(fpatch_lua PUBLIC ${LUA_SRC_DIR})
# Lua's POSIX mode requires fseeko/ftello, which Bionic exposes from API 24.
# The API 21 runtime uses Lua's ANSI stdio fallback instead.
target_compile_options(fpatch_lua PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
)

add_library(falconpatch SHARED
    src/archive.c
    src/jni_bridge.c
    src/lua_runtime.c
    src/module_loader.c
    src/runtime.c
    src/modules/app_module.c
    src/modules/assets_module.c
    src/modules/events_module.c
    src/modules/gui_module.c
    src/modules/intent_module.c
    src/modules/jni_module.c
    src/modules/ui_module.c
)

target_include_directories(falconpatch
    PUBLIC include
    PRIVATE src ${LUA_SRC_DIR}
)
target_compile_definitions(falconpatch PRIVATE FPATCH_ANDROID_VERSION="${PROJECT_VERSION}")
target_compile_definitions(falconpatch PRIVATE FPATCH_ANDROID_ABI="${ANDROID_ABI}")
target_compile_options(falconpatch PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)
target_link_options(falconpatch PRIVATE "LINKER:--export-dynamic")
target_link_libraries(falconpatch PRIVATE fpatch_lua android log dl)

set_target_properties(falconpatch PROPERTIES OUTPUT_NAME falconpatch)
