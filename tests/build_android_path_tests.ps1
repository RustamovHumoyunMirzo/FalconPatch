[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) `
    ".fpatch-android-path-test-$([guid]::NewGuid().ToString('N'))"
$previousSdkRoot = $env:ANDROID_SDK_ROOT
$previousAndroidHome = $env:ANDROID_HOME
$previousLocalAppData = $env:LOCALAPPDATA

New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $androidCmake = Get-Content -Raw (Join-Path $repoRoot "android/CMakeLists.txt")
    $androidBuildScript = Get-Content -Raw (Join-Path $repoRoot "scripts/build_android.ps1")
    $vendoredLuaHeader = Get-Content -Raw (Join-Path $repoRoot "third_party/lua/lua.h")
    if ($androidCmake -match 'FetchContent|https?://') {
        throw "The Android build must not download dependencies during CMake configuration."
    }
    if ($androidCmake -match 'target_compile_definitions\s*\(\s*fpatch_lua[^\)]*LUA_USE_POSIX') {
        throw "Lua POSIX mode requires Android API 24 and cannot be used by the API 21 runtime."
    }
    if ($androidBuildScript -match 'FETCHCONTENT_SOURCE_DIR_LUA_SOURCE|luaSourceCache') {
        throw "The Android build script still contains obsolete downloaded-Lua cache handling."
    }
    if ($androidCmake -notmatch 'third_party/lua') {
        throw "The Android CMake project does not use the vendored Lua source."
    }
    if ($vendoredLuaHeader -notmatch '#define\s+LUA_VERSION_MAJOR\s+"5"' -or
        $vendoredLuaHeader -notmatch '#define\s+LUA_VERSION_MINOR\s+"4"' -or
        $vendoredLuaHeader -notmatch '#define\s+LUA_VERSION_RELEASE\s+"8"' -or
        $vendoredLuaHeader -notmatch 'Permission is hereby granted, free of charge') {
        throw "The vendored Lua source is not the expected licensed Lua 5.4.8 release."
    }

    $env:ANDROID_SDK_ROOT = $testRoot
    $env:ANDROID_HOME = $null
    $env:LOCALAPPDATA = $null

    & (Join-Path $repoRoot "scripts/build_android.ps1") -BootstrapOnly
    Write-Host "Android build path regression test passed."
} finally {
    $env:ANDROID_SDK_ROOT = $previousSdkRoot
    $env:ANDROID_HOME = $previousAndroidHome
    $env:LOCALAPPDATA = $previousLocalAppData

    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    $resolvedWork = [IO.Path]::GetFullPath($WorkDirectory).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTest.StartsWith($resolvedWork, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTest)) {
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
