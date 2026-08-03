[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [ValidateSet("armeabi-v7a", "arm64-v8a", "x86", "x86_64")]
    [string[]]$Abis = @("armeabi-v7a", "arm64-v8a", "x86", "x86_64"),
    [ValidateRange(21, 100)]
    [int]$Api = 21,
    [switch]$BootstrapOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repoRoot "android"
$buildRoot = Join-Path $repoRoot "build/android"
$distRoot = Join-Path $repoRoot "dist/android"
$luaSourceCache = $null

function Resolve-AndroidSdk {
    $candidates = @($env:ANDROID_SDK_ROOT, $env:ANDROID_HOME)
    if ($IsWindows -and $env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA "Android/Sdk"
    } elseif ($IsMacOS -and $HOME) {
        $candidates += Join-Path $HOME "Library/Android/sdk"
    } elseif ($HOME) {
        $candidates += Join-Path $HOME "Android/Sdk"
    }
    $candidates = $candidates | Where-Object { $_ -and (Test-Path $_) }
    return $candidates | Select-Object -First 1
}

function Get-LatestDirectory([string]$Parent) {
    if (-not (Test-Path $Parent)) {
        return $null
    }
    return Get-ChildItem -Path $Parent -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]"0.0" } } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

function Resolve-Ndk([string]$SdkRoot) {
    $candidates = @($env:ANDROID_NDK_HOME, $env:ANDROID_NDK_ROOT) |
        Where-Object { $_ -and (Test-Path (Join-Path $_ "build/cmake/android.toolchain.cmake")) }
    if ($candidates) {
        return $candidates | Select-Object -First 1
    }
    $sideBySide = Get-LatestDirectory (Join-Path $SdkRoot "ndk")
    if ($sideBySide -and (Test-Path (Join-Path $sideBySide "build/cmake/android.toolchain.cmake"))) {
        return $sideBySide
    }
    $legacy = Join-Path $SdkRoot "ndk-bundle"
    if (Test-Path (Join-Path $legacy "build/cmake/android.toolchain.cmake")) {
        return $legacy
    }
    return $null
}

function Resolve-JavaTool([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    if ($env:JAVA_HOME) {
        $suffix = if ($env:OS -eq "Windows_NT") { "$Name.exe" } else { $Name }
        $candidate = Join-Path $env:JAVA_HOME "bin/$suffix"
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Build-BootstrapDex([string]$SdkRoot) {
    $javac = Resolve-JavaTool "javac"
    $jarTool = Resolve-JavaTool "jar"
    if (-not $javac -or -not $jarTool) {
        Write-Warning "Java was not found. Bootstrap DEX generation was skipped."
        return
    }

    $platform = Get-LatestDirectory (Join-Path $SdkRoot "platforms")
    $buildTools = Get-LatestDirectory (Join-Path $SdkRoot "build-tools")
    if (-not $platform -or -not $buildTools) {
        Write-Warning "Android platforms/build-tools are missing. Bootstrap DEX generation was skipped."
        return
    }
    $androidJar = Join-Path $platform "android.jar"
    $d8 = Join-Path $buildTools $(if ($env:OS -eq "Windows_NT") { "d8.bat" } else { "d8" })
    if (-not (Test-Path $androidJar) -or -not (Test-Path $d8)) {
        Write-Warning "android.jar or d8 was not found. Bootstrap DEX generation was skipped."
        return
    }

    $bootstrapBuild = Join-Path $buildRoot "bootstrap"
    $javaClasses = Join-Path $bootstrapBuild "java-classes"
    $javaDex = Join-Path $bootstrapBuild "java-dex"
    New-Item -ItemType Directory -Force -Path $javaClasses, $javaDex | Out-Null
    $javaSources = Get-ChildItem -Path (Join-Path $androidRoot "java") -Recurse -Filter *.java |
        Select-Object -ExpandProperty FullName
    & $javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $androidJar -d $javaClasses @javaSources
    if ($LASTEXITCODE -ne 0) {
        throw "Java bootstrap compilation failed."
    }
    $javaArchive = Join-Path $bootstrapBuild "bootstrap-java.jar"
    & $jarTool cf $javaArchive -C $javaClasses .
    if ($LASTEXITCODE -ne 0) {
        throw "Java bootstrap archive creation failed."
    }
    & $d8 --lib $androidJar --min-api $Api --output $javaDex $javaArchive
    if ($LASTEXITCODE -ne 0) {
        throw "Java bootstrap DEX generation failed."
    }
    $javaDist = Join-Path $distRoot "bootstrap/java"
    New-Item -ItemType Directory -Force -Path $javaDist | Out-Null
    Copy-Item -Force (Join-Path $javaDex "classes.dex") (Join-Path $javaDist "classes.dex")

    $kotlinc = Get-Command kotlinc -ErrorAction SilentlyContinue
    if (-not $kotlinc) {
        Write-Warning "kotlinc was not found. Kotlin bootstrap generation was skipped; Java remains available."
        return
    }
    $kotlinClasses = Join-Path $bootstrapBuild "kotlin-classes"
    $kotlinDex = Join-Path $bootstrapBuild "kotlin-dex"
    New-Item -ItemType Directory -Force -Path $kotlinClasses, $kotlinDex | Out-Null
    $kotlinSources = Get-ChildItem -Path (Join-Path $androidRoot "kotlin") -Recurse -Filter *.kt |
        Select-Object -ExpandProperty FullName
    $classpathSeparator = [IO.Path]::PathSeparator
    & $kotlinc.Source @kotlinSources `
        -classpath "$androidJar$classpathSeparator$javaClasses" `
        -jvm-target 1.8 `
        -Xno-param-assertions `
        -Xno-call-assertions `
        -Xno-receiver-assertions `
        -d $kotlinClasses
    if ($LASTEXITCODE -ne 0) {
        throw "Kotlin bootstrap compilation failed."
    }
    $kotlinArchive = Join-Path $bootstrapBuild "bootstrap-kotlin.jar"
    & $jarTool cf $kotlinArchive -C $javaClasses .
    & $jarTool uf $kotlinArchive -C $kotlinClasses .
    & $d8 --lib $androidJar --min-api $Api --output $kotlinDex $kotlinArchive
    if ($LASTEXITCODE -ne 0) {
        throw "Kotlin bootstrap DEX generation failed."
    }
    $kotlinDist = Join-Path $distRoot "bootstrap/kotlin"
    New-Item -ItemType Directory -Force -Path $kotlinDist | Out-Null
    Copy-Item -Force (Join-Path $kotlinDex "classes.dex") (Join-Path $kotlinDist "classes.dex")
}

$sdkRoot = Resolve-AndroidSdk
if (-not $sdkRoot) {
    throw "Android SDK not found. Set ANDROID_SDK_ROOT or ANDROID_HOME."
}
Build-BootstrapDex $sdkRoot
if ($BootstrapOnly) {
    Write-Host "Bootstrap artifacts are ready under $distRoot/bootstrap"
    return
}
$ndkRoot = Resolve-Ndk $sdkRoot
if (-not $ndkRoot) {
    throw "Android NDK not found. Install an NDK side-by-side package or set ANDROID_NDK_HOME."
}
$toolchain = Join-Path $ndkRoot "build/cmake/android.toolchain.cmake"

New-Item -ItemType Directory -Force -Path $buildRoot, $distRoot | Out-Null
foreach ($abi in $Abis) {
    $abiBuild = Join-Path $buildRoot $abi
    $abiDist = Join-Path $distRoot $abi
    New-Item -ItemType Directory -Force -Path $abiBuild, $abiDist | Out-Null
    $configureArguments = @(
        "-S", $androidRoot,
        "-B", $abiBuild,
        "-G", "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DANDROID_ABI=$abi",
        "-DANDROID_PLATFORM=android-$Api",
        "-DCMAKE_BUILD_TYPE=$Configuration"
    )
    if ($luaSourceCache) {
        $configureArguments += "-DFETCHCONTENT_SOURCE_DIR_LUA_SOURCE=$luaSourceCache"
    }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Android configuration failed for $abi."
    }
    & cmake --build $abiBuild --target falconpatch
    if ($LASTEXITCODE -ne 0) {
        throw "Android runtime build failed for $abi."
    }
    $library = Get-ChildItem -Path $abiBuild -Recurse -Filter libfalconpatch.so |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $library) {
        throw "libfalconpatch.so was not found for $abi."
    }
    Copy-Item -Force $library (Join-Path $abiDist "libfalconpatch.so")
    Write-Host "Built Android runtime for $abi"
    if (-not $luaSourceCache) {
        $luaSourceCache = Get-ChildItem -Path $abiBuild -Recurse -Filter lua.h |
            Where-Object { $_.Directory.Name -eq "src" } |
            Select-Object -First 1 -ExpandProperty DirectoryName
        if (-not $luaSourceCache) {
            throw "The downloaded Lua source directory was not found."
        }
    }
}

$sdkInclude = Join-Path $distRoot "sdk/include"
New-Item -ItemType Directory -Force -Path $sdkInclude | Out-Null
Copy-Item -Force (Join-Path $androidRoot "include/FalconPatch.h") $sdkInclude
if ($luaSourceCache) {
    Copy-Item -Force (Join-Path $luaSourceCache "lua.h") $sdkInclude
    Copy-Item -Force (Join-Path $luaSourceCache "luaconf.h") $sdkInclude
    Copy-Item -Force (Join-Path $luaSourceCache "lauxlib.h") $sdkInclude
    Copy-Item -Force (Join-Path $luaSourceCache "lualib.h") $sdkInclude
}

Write-Host "Android artifacts are ready under $distRoot"
