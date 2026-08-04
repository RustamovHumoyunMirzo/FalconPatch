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
$expectedLuaHash = "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae"

New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $androidCmake = Get-Content -Raw (Join-Path $repoRoot "android/CMakeLists.txt")
    $androidBuildScript = Get-Content -Raw (Join-Path $repoRoot "scripts/build_android.ps1")
    if ($androidCmake -notmatch "URL_HASH\s+SHA256=$expectedLuaHash") {
        throw "Lua 5.4.8 does not use the verified upstream SHA-256."
    }
    if ($androidCmake -match 'target_compile_definitions\s*\(\s*fpatch_lua[^\)]*LUA_USE_POSIX') {
        throw "Lua POSIX mode requires Android API 24 and cannot be used by the API 21 runtime."
    }
    if ($androidBuildScript -notmatch '\$luaSourceCache\s*=\s*Split-Path\s+-Parent\s+\$luaIncludeDirectory') {
        throw "The cross-ABI Lua cache must point at the extracted source root."
    }
    if ($androidCmake -notmatch 'EXISTS\s+"\$\{lua_source_SOURCE_DIR\}/src/lapi\.c"') {
        throw "The Android CMake project does not validate the Lua source layout."
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
