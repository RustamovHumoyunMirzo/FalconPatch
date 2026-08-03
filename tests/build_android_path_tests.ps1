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
