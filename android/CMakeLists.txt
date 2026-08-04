cmake_minimum_required(VERSION 3.22)

project(FalconPatchAndroid VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
    lua_source
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua_source)

if(EXISTS "${lua_source_SOURCE_DIR}/src/lapi.c")
    set(LUA_SRC_DIR "${lua_source_SOURCE_DIR}/src")
elseif(EXISTS "${lua_source_SOURCE_DIR}/lapi.c")
    set(LUA_SRC_DIR "${lua_source_SOURCE_DIR}")
else()
    message(FATAL_ERROR
        "Lua source directory does not contain src/lapi.c or lapi.c: ${lua_source_SOURCE_DIR}"
    )
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
target_compile_options(fpatch_lua PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_library(falconpatch SHARED
    src/archive.c
    src/jni_bridge.c
    src/lua_runtime.c
    src/module_loader.c
    src/runtime.c
    src/modules/background_worker.c
    src/modules/gui_module.c
    src/modules/jni_module.c
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
