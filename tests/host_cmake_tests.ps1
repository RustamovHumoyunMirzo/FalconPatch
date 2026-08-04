[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$rootCmake = Get-Content -Raw (Join-Path $repoRoot "CMakeLists.txt")

if ($rootCmake -notmatch 'target_compile_definitions\s*\(\s*yaml[\s\S]*_POSIX_C_SOURCE=200809L') {
    throw "The Linux host build must expose POSIX strdup() while compiling vendored libyaml."
}

if ($rootCmake -notmatch 'target_compile_definitions\s*\(\s*zip[\s\S]*_GNU_SOURCE') {
    throw "The Linux host build must expose GNU/POSIX types while compiling vendored libzip."
}

Write-Host "Host CMake flag regression test passed."
