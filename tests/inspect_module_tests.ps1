[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fpatch,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$testRoot = Join-Path ([IO.Path]::GetFullPath($WorkDirectory)) `
    ".fpatch-inspect-module-test-$([guid]::NewGuid().ToString('N'))"
$inputRoot = Join-Path $testRoot "input"
$apk = Join-Path $testRoot "fixture.apk"

function Write-Fixture([string]$Path, [string]$Value) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}

New-Item -ItemType Directory -Path $testRoot, $inputRoot | Out-Null
try {
    Write-Fixture (Join-Path $inputRoot "AndroidManifest.xml") "manifest"
    Write-Fixture (Join-Path $inputRoot "classes.dex") "not-a-real-dex"
    Write-Fixture (Join-Path $inputRoot "lib/arm64-v8a/libdemo.so") @"
ELF-ish
Java_com_example_Bridge_ping
RegisterNatives
com/example/Bridge
ping
()I
liblog.so
"@
    Write-Fixture (Join-Path $inputRoot "lib/arm64-v8a/libcaller.so") "dlopen libdemo.so demo"
    Write-Fixture (Join-Path $inputRoot "lib/x86_64/libdemo.so") "x86 copy"
    [IO.Compression.ZipFile]::CreateFromDirectory($inputRoot, $apk)

    $output = & $Fpatch inspect-module --source $apk --target demo --abi arm64-v8a 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "inspect-module command failed:`n$output"
    }
    $text = $output -join "`n"
    if ($text -notmatch "FalconPatch module inspection" -or
        $text -notmatch "Target: libdemo\.so" -or
        $text -notmatch "lib/arm64-v8a/libdemo\.so" -or
        $text -notmatch "Static JNI exports: 1" -or
        $text -notmatch "RegisterNatives: detected" -or
        $text -notmatch "lib/arm64-v8a/libcaller\.so" -or
        $text -notmatch "liblog\.so") {
        throw "inspect-module output missed expected evidence:`n$text"
    }
    Write-Host "Inspect-module integration test passed."
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
