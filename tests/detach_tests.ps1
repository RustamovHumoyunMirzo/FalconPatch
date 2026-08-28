[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fpatch,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$testRoot = Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) `
    ".fpatch-detach-test-$([guid]::NewGuid().ToString('N'))"
$inputRoot = Join-Path $testRoot "input"
$apk = Join-Path $testRoot "fixture.apk"
$out = Join-Path $testRoot "fixture-detached.apk"

function Write-Fixture([string]$Path, [string]$Value) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}

function Has-Entry([string]$ZipPath, [string]$Entry) {
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        return $null -ne $zip.GetEntry($Entry)
    } finally {
        $zip.Dispose()
    }
}

New-Item -ItemType Directory -Path $testRoot, $inputRoot | Out-Null
try {
    Write-Fixture (Join-Path $inputRoot "AndroidManifest.xml") "manifest"
    Write-Fixture (Join-Path $inputRoot "classes.dex") "dex"
    Write-Fixture (Join-Path $inputRoot "lib/arm64-v8a/libdemo.so") "arm64"
    Write-Fixture (Join-Path $inputRoot "lib/x86_64/libdemo.so") "x86_64"
    Write-Fixture (Join-Path $inputRoot "lib/arm64-v8a/libkeep.so") "keep"
    Write-Fixture (Join-Path $inputRoot "META-INF/CERT.SF") "old-signature"
    [IO.Compression.ZipFile]::CreateFromDirectory($inputRoot, $apk)

    $output = & $Fpatch detach --target $apk --so demo --abi arm64-v8a --out $out 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "detach command failed:`n$output"
    }
    if (Has-Entry $out "lib/arm64-v8a/libdemo.so") {
        throw "detach did not remove the selected ABI library."
    }
    if (-not (Has-Entry $out "lib/x86_64/libdemo.so")) {
        throw "detach removed an ABI that was not selected."
    }
    if (-not (Has-Entry $out "lib/arm64-v8a/libkeep.so")) {
        throw "detach removed an unrelated library."
    }
    if (Has-Entry $out "META-INF/CERT.SF") {
        throw "detach kept stale APK signature entries."
    }

    $smartOut = Join-Path $testRoot "fixture-smart-detached.apk"
    $smart = & $Fpatch detach --target $apk --so demo --out $smartOut --smart-repair 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "detach --smart-repair failed:`n$smart"
    }
    if (($smart -join "`n") -notmatch "Smart repair load calls: 0") {
        throw "detach --smart-repair did not report repaired load calls."
    }
    Write-Host "Detach integration test passed."
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
