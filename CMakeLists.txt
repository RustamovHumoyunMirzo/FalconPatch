cmake_minimum_required(VERSION 3.24)

project(FalconPatch
    VERSION 1.8.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_OSSFUZZ OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
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

FetchContent_Declare(
    libyaml
    GIT_REPOSITORY https://github.com/yaml/libyaml.git
    GIT_TAG 0.2.5
    SOURCE_SUBDIR _fpatch_source_only
)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
FetchContent_MakeAvailable(libzip)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET zip)
    target_compile_definitions(zip PRIVATE _GNU_SOURCE)
endif()
FetchContent_MakeAvailable(libyaml)

add_library(yaml STATIC
    "${libyaml_SOURCE_DIR}/src/api.c"
    "${libyaml_SOURCE_DIR}/src/dumper.c"
    "${libyaml_SOURCE_DIR}/src/emitter.c"
    "${libyaml_SOURCE_DIR}/src/loader.c"
    "${libyaml_SOURCE_DIR}/src/parser.c"
    "${libyaml_SOURCE_DIR}/src/reader.c"
    "${libyaml_SOURCE_DIR}/src/scanner.c"
    "${libyaml_SOURCE_DIR}/src/writer.c"
)
target_include_directories(yaml PUBLIC "${libyaml_SOURCE_DIR}/include")
target_compile_definitions(yaml
    PUBLIC YAML_DECLARE_STATIC
    PRIVATE
        YAML_VERSION_MAJOR=0
        YAML_VERSION_MINOR=2
        YAML_VERSION_PATCH=5
        YAML_VERSION_STRING="0.2.5"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(yaml PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(MSVC)
    target_compile_definitions(yaml PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(yaml PRIVATE /W0)
endif()

file(GLOB COMMAND_SOURCES "src/commands/*.c")
file(GLOB UTIL_SOURCES "src/utils/*.c")
file(GLOB EMBEDDED_ANDROID_ARTIFACTS CONFIGURE_DEPENDS
    "dist/android/*/libfalconpatch.so"
    "dist/android/bootstrap/*/classes.dex")

set(EMBEDDED_RESOURCES_C "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_resources.c")
add_custom_command(
    OUTPUT "${EMBEDDED_RESOURCES_C}"
    COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT=${EMBEDDED_RESOURCES_C}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbedded.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbedded.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/android/java/dev/falconpatch/runtime/RuntimeBridge.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/android/java/dev/falconpatch/runtime/FalconPatchProvider.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/android/java/dev/falconpatch/runtime/FalconPatchService.java"
        "${CMAKE_CURRENT_SOURCE_DIR}/android/kotlin/dev/falconpatch/runtime/FalconPatchKotlinProvider.kt"
        "${CMAKE_CURRENT_SOURCE_DIR}/android/kotlin/dev/falconpatch/runtime/FalconPatchKotlinService.kt"
        ${EMBEDDED_ANDROID_ARTIFACTS}
    VERBATIM
)

add_executable(fpatch
    src/main.c
    src/cli.c
    ${COMMAND_SOURCES}
    ${UTIL_SOURCES}
    "${EMBEDDED_RESOURCES_C}"
)

target_include_directories(fpatch PRIVATE include)
target_compile_definitions(fpatch PRIVATE FPATCH_VERSION="${PROJECT_VERSION}")
target_link_libraries(fpatch PRIVATE zip yaml ZLIB::ZLIB)

if(MSVC)
    target_compile_definitions(fpatch PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(fpatch PRIVATE /W4)
else()
    target_compile_options(fpatch PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(FPATCH_BUILD_TESTS "Build FalconPatch host tests" ON)
if(FPATCH_BUILD_TESTS)
    enable_testing()
    add_executable(fpatch_profile_tests
        tests/profile_tests.c
        src/utils/file_utils.c
        src/utils/inject_profile.c
        src/utils/payload_archive.c
    )
    target_include_directories(fpatch_profile_tests PRIVATE include)
    target_compile_definitions(fpatch_profile_tests PRIVATE
        FPATCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(fpatch_profile_tests PRIVATE yaml)
    if(MSVC)
        target_compile_definitions(fpatch_profile_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(fpatch_profile_tests PRIVATE /W4)
    else()
        target_compile_options(fpatch_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME profile-and-payload COMMAND fpatch_profile_tests)
    add_executable(fpatch_artifact_tests
        tests/artifact_bundle_tests.c
        src/utils/artifact_bundle.c
        src/utils/sha256.c
    )
    target_include_directories(fpatch_artifact_tests PRIVATE include)
    target_link_libraries(fpatch_artifact_tests PRIVATE yaml ZLIB::ZLIB)
    if(MSVC)
        target_compile_definitions(fpatch_artifact_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(fpatch_artifact_tests PRIVATE /W4)
    else()
        target_compile_options(fpatch_artifact_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME artifact-bundle COMMAND fpatch_artifact_tests)
    add_executable(fpatch_dex_repair_tests
        tests/dex_repair_tests.c
        src/utils/apk_detacher.c
        src/utils/apk_signer.c
        src/utils/file_utils.c
        src/utils/inject_profile.c
        src/utils/process.c
        src/utils/sha256.c
    )
    target_include_directories(fpatch_dex_repair_tests PRIVATE include)
    target_link_libraries(fpatch_dex_repair_tests PRIVATE zip yaml)
    if(MSVC)
        target_compile_definitions(fpatch_dex_repair_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(fpatch_dex_repair_tests PRIVATE /W4)
    else()
        target_compile_options(fpatch_dex_repair_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME dex-smart-repair COMMAND fpatch_dex_repair_tests)
    find_program(FPATCH_PWSH_EXECUTABLE NAMES pwsh)
    if(FPATCH_PWSH_EXECUTABLE)
        add_test(NAME host-cmake-flags
            COMMAND "${FPATCH_PWSH_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/host_cmake_tests.ps1")
        add_test(NAME android-script-paths
            COMMAND "${FPATCH_PWSH_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_android_path_tests.ps1"
                -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME artifact-packager
            COMMAND "${FPATCH_PWSH_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_artifacts_tests.ps1"
                -Fpatch "$<TARGET_FILE:fpatch>"
                -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME cli-detach
            COMMAND "${FPATCH_PWSH_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/detach_tests.ps1"
                -Fpatch "$<TARGET_FILE:fpatch>"
                -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME cli-inspect-module
            COMMAND "${FPATCH_PWSH_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_module_tests.ps1"
                -Fpatch "$<TARGET_FILE:fpatch>"
                -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    add_test(NAME cli-version COMMAND fpatch --version)
    add_test(NAME cli-inject-dry-run
             COMMAND fpatch inject --profile "${CMAKE_CURRENT_SOURCE_DIR}/examples/example_fp_profile.yaml" --dry-run)
endif()
