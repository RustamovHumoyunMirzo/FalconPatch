[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fpatch,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) `
    ".fpatch-packager-test-$([guid]::NewGuid().ToString('N'))"
$androidRoot = Join-Path $testRoot "android"
$outputRoot = Join-Path $testRoot "packages"

function Write-Fixture([string]$Path, [string]$Value) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}

New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    foreach ($abi in @("armeabi-v7a", "arm64-v8a", "x86", "x86_64")) {
        Write-Fixture (Join-Path $androidRoot "$abi/libfalconpatch.so") "runtime-$abi"
    }
    foreach ($language in @("java", "kotlin")) {
        Write-Fixture (Join-Path $androidRoot "bootstrap/$language/classes.dex") `
            "bootstrap-$language"
    }
    foreach ($header in @("FalconPatch.h", "lua.h", "luaconf.h", "lauxlib.h", "lualib.h")) {
        Write-Fixture (Join-Path $androidRoot "sdk/include/$header") "header-$header"
    }

    & (Join-Path $repoRoot "scripts/package_artifacts.ps1") `
        -Platform windows `
        -Arch x86_64 `
        -HostExecutable $Fpatch `
        -AndroidArtifactsRoot $androidRoot `
        -OutputDirectory $outputRoot | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Artifact packaging command failed."
    }

    $archive = Join-Path $outputRoot "windows-x86_64.tar.gz"
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "Expected package was not created: $archive"
    }
    $output = & $Fpatch inject `
        --source placeholder.apk `
        --artifacts $archive `
        --dry-run 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "fpatch rejected a package produced by package_artifacts.ps1:`n$output"
    }
    if (($output -join "`n") -notmatch "Artifacts: windows-x86_64") {
        throw "Dry-run output did not identify the package metadata."
    }
    Write-Host "Artifact packaging integration test passed."
} finally {
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
