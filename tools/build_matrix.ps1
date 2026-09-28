[CmdletBinding()]
param(
    [string]$InstallRoot = "C:\Program Files\Side Effects Software",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $projectRoot "build\compatibility"
}

$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

$installs = Get-ChildItem -LiteralPath $InstallRoot -Directory -Filter "Houdini *" |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName "toolkit\include\SYS\SYS_Version.h")) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "bin\Qt6Core.dll"))
    } |
    Sort-Object Name

if (-not $installs) {
    throw "No Houdini Qt 6 HDK installations were found under $InstallRoot"
}

$previousHfs = $env:HFS
$previousQt = $env:QT6_BASELINE
$previousOut = $env:HOUDINI_CHINESE_OUT
$built = 0

try {
    foreach ($install in $installs) {
        $qtRuntime = Get-Item -LiteralPath (Join-Path $install.FullName "bin\Qt6Core.dll")
        $qtVersion = [version]$qtRuntime.VersionInfo.FileVersion
        $qtRelease = "{0}.{1}.{2}" -f $qtVersion.Major, $qtVersion.Minor, $qtVersion.Build
        $qtRoot = Join-Path (Split-Path -Parent $projectRoot) "_ThirdParty\Qt\$qtRelease"
        $qtSdk = Get-ChildItem -LiteralPath $qtRoot -Directory -Filter "msvc*_64" -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "lib\Qt6Core.lib") } |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if (-not $qtSdk) {
            Write-Warning "Skipping $($install.Name): matching Qt $qtRelease SDK not found."
            continue
        }

        $targetName = $install.Name -replace "[^0-9A-Za-z._-]", "_"
        $env:HFS = $install.FullName
        $env:QT6_BASELINE = $qtSdk.FullName
        $env:HOUDINI_CHINESE_OUT = Join-Path $OutputRoot $targetName
        & (Join-Path $PSScriptRoot "build.bat")
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for $($install.Name) with exit code $LASTEXITCODE"
        }
        $built++
    }
}
finally {
    $env:HFS = $previousHfs
    $env:QT6_BASELINE = $previousQt
    $env:HOUDINI_CHINESE_OUT = $previousOut
}

if ($built -eq 0) {
    throw "No compatible Houdini/Qt target could be built."
}
Write-Host "Built $built compatible Houdini package(s) under $OutputRoot"
