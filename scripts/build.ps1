[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirectory = "build"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDirectory
$distPath = Join-Path $repoRoot "dist"

New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
New-Item -ItemType Directory -Force -Path $distPath | Out-Null

& cmake --no-warn-unused-cli -S $repoRoot -B $buildPath "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

& cmake --build $buildPath --config $Configuration --target fpatch
if ($LASTEXITCODE -ne 0) {
    throw "FalconPatch host build failed."
}

$executableName = if ($IsWindows -or $env:OS -eq "Windows_NT") { "fpatch.exe" } else { "fpatch" }
$candidates = @(
    (Join-Path $buildPath $executableName),
    (Join-Path (Join-Path $buildPath $Configuration) $executableName)
)
$executable = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $executable) {
    $executable = Get-ChildItem -Path $buildPath -Recurse -File -Filter $executableName |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $executable) {
    throw "The fpatch executable was not found after a successful build."
}

Copy-Item -Force $executable (Join-Path $distPath $executableName)
Write-Host "Built FalconPatch host CLI: $(Join-Path $distPath $executableName)"
