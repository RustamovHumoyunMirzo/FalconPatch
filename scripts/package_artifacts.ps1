[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("windows", "linux", "macos")]
    [string]$Platform,

    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64", "arm64")]
    [string]$Arch,

    [string]$HostExecutable,
    [string]$AndroidArtifactsRoot,
    [string]$OutputDirectory,
    [string]$Version
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $HostExecutable) {
    $hostName = if ($Platform -eq "windows") { "fpatch.exe" } else { "fpatch" }
    $HostExecutable = Join-Path $repoRoot "dist/$hostName"
}
if (-not $AndroidArtifactsRoot) {
    $AndroidArtifactsRoot = Join-Path $repoRoot "dist/android"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot "dist/packages"
}
if (-not $Version) {
    $cmakeText = Get-Content -Raw (Join-Path $repoRoot "CMakeLists.txt")
    $versionMatch = [regex]::Match(
        $cmakeText,
        'project\s*\(\s*FalconPatch[\s\S]*?VERSION\s+([0-9]+(?:\.[0-9]+){1,3})',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
    )
    if (-not $versionMatch.Success) {
        throw "FalconPatch version was not found in CMakeLists.txt. Pass -Version explicitly."
    }
    $Version = $versionMatch.Groups[1].Value
}

$HostExecutable = [IO.Path]::GetFullPath($HostExecutable)
$AndroidArtifactsRoot = [IO.Path]::GetFullPath($AndroidArtifactsRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $HostExecutable -PathType Leaf)) {
    throw "Host executable not found: $HostExecutable"
}
if (-not (Test-Path -LiteralPath $AndroidArtifactsRoot -PathType Container)) {
    throw "Android artifact directory not found: $AndroidArtifactsRoot"
}
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    throw "tar is required to create the artifact package."
}

$packageName = "$Platform-$Arch"
$archivePath = Join-Path $OutputDirectory "$packageName.tar.gz"
$operationId = [guid]::NewGuid().ToString('N')
$archiveTempPath = Join-Path $OutputDirectory ".$packageName-$operationId.tar.gz"
$stageRoot = Join-Path $OutputDirectory ".fpatch-package-$packageName-$operationId"
$files = [Collections.Generic.List[object]]::new()

function Add-PackageFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required artifact is missing: $Source"
    }
    $normalizedPath = $RelativePath.Replace('\', '/')
    $destination = Join-Path $stageRoot $normalizedPath
    $parent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination -Force
    if ($Kind -eq "executable" -and $Platform -ne "windows") {
        & chmod +x $destination
        if ($LASTEXITCODE -ne 0) {
            throw "Could not mark the packaged host executable as executable."
        }
    }
    $stagedFile = Get-Item -LiteralPath $destination
    $files.Add([ordered]@{
        kind = $Kind
        name = $Name
        path = $normalizedPath
        size = $stagedFile.Length
        sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Path $stageRoot | Out-Null

try {
    $hostName = if ($Platform -eq "windows") { "fpatch.exe" } else { "fpatch" }
    Add-PackageFile -Source $HostExecutable -RelativePath "host/$hostName" `
        -Kind "executable" -Name "fpatch"
    Add-PackageFile -Source (Join-Path $repoRoot "LICENSE") -RelativePath "LICENSE" `
        -Kind "license" -Name "FalconPatch"

    $abis = @("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
    foreach ($abi in $abis) {
        Add-PackageFile `
            -Source (Join-Path $AndroidArtifactsRoot "$abi/libfalconpatch.so") `
            -RelativePath "android/runtime/$abi/libfalconpatch.so" `
            -Kind "runtime" -Name $abi
    }

    foreach ($language in @("java", "kotlin")) {
        Add-PackageFile `
            -Source (Join-Path $AndroidArtifactsRoot "bootstrap/$language/classes.dex") `
            -RelativePath "android/bootstrap/$language/classes.dex" `
            -Kind "bootstrap-dex" -Name $language
    }

    foreach ($header in @("FalconPatch.h", "lua.h", "luaconf.h", "lauxlib.h", "lualib.h")) {
        Add-PackageFile `
            -Source (Join-Path $AndroidArtifactsRoot "sdk/include/$header") `
            -RelativePath "android/sdk/include/$header" `
            -Kind "sdk-header" -Name $header
    }

    $manifest = [ordered]@{
        schema_version = 1
        runtime_api = 1
        falconpatch_version = $Version
        package = $packageName
        host = [ordered]@{
            platform = $Platform
            arch = $Arch
        }
        files = @($files)
    }
    $metadataPath = Join-Path $stageRoot "falconpatch-artifacts.json"
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $metadataPath -Encoding utf8NoBOM

    Push-Location $stageRoot
    try {
        $previousCopyfileDisable = $env:COPYFILE_DISABLE
        $env:COPYFILE_DISABLE = "1"
        & tar -czf $archiveTempPath "falconpatch-artifacts.json" "LICENSE" "host" "android"
        if ($LASTEXITCODE -ne 0) {
            throw "tar failed while creating $archivePath"
        }
    } finally {
        $env:COPYFILE_DISABLE = $previousCopyfileDisable
        Pop-Location
    }
    if (-not (Test-Path -LiteralPath $archiveTempPath -PathType Leaf)) {
        throw "Artifact package was not created: $archivePath"
    }
    Move-Item -LiteralPath $archiveTempPath -Destination $archivePath -Force
    Write-Host "Created FalconPatch artifact package: $archivePath"
    Write-Output $archivePath
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stageRoot)
    $resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedStage.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    if (Test-Path -LiteralPath $archiveTempPath) {
        Remove-Item -LiteralPath $archiveTempPath -Force
    }
}
