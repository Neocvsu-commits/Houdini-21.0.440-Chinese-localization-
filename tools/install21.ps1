param([string]$HoudiniRoot = '', [string]$PackageDirectory = '')
$ErrorActionPreference = 'Stop'
$release = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $HoudiniRoot) {
    foreach ($candidate in @('D:\DCC SoftWare\Houdini 21.0.440', 'C:\Program Files\Side Effects Software\Houdini 21.0.440')) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\houdini.exe')) { $HoudiniRoot = $candidate; break }
    }
}
$exe = Join-Path $HoudiniRoot 'bin\houdini.exe'
$version = (Get-Item -LiteralPath $exe).VersionInfo
if ($version.FileMajorPart -ne 21 -or $version.FileMinorPart -ne 0 -or $version.FilePrivatePart -ne 440) {
    throw 'This package requires Houdini 21.0.440.'
}
$running = Get-Process -Name houdini,houdinifx,houdinicore -ErrorAction SilentlyContinue
if ($running) { throw 'Close Houdini before installing. No process has been stopped by this installer.' }
if (-not $PackageDirectory) {
    $pref = $env:HOUDINI_USER_PREF_DIR
    if (-not $pref) { $pref = [Environment]::GetEnvironmentVariable('HOUDINI_USER_PREF_DIR','User') }
    if (-not $pref) { $pref = [Environment]::GetEnvironmentVariable('HOUDINI_USER_PREF_DIR','Machine') }
    if ($pref) { $pref = [Environment]::ExpandEnvironmentVariables($pref).Replace('__HVER__','21.0') }
    else { $pref = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'houdini21.0' }
    $PackageDirectory = Join-Path $pref 'packages'
}
$target = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\','/')
$program = [IO.Path]::GetFullPath($HoudiniRoot).TrimEnd('\','/')
if ($target.Equals($program,[StringComparison]::OrdinalIgnoreCase) -or $target.StartsWith($program+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installation into the Houdini program directory is prohibited.'
}
$manifest = Get-Content -LiteralPath (Join-Path $release 'checksums.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($entry in $manifest.PSObject.Properties) {
    $file = [IO.Path]::GetFullPath((Join-Path $release $entry.Name))
    if (-not $file.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe manifest path.' }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Value) { throw "Checksum mismatch: $($entry.Name)" }
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
$backup = Join-Path (Split-Path -Parent $target) ('HoudiniChinese-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$names = @('HoudiniChinese.json','HoudiniChinese')
$moved = @()
foreach ($name in $names) {
    $existing = [IO.Path]::GetFullPath((Join-Path $target $name))
    if (-not $existing.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe target path.' }
    if (Test-Path -LiteralPath $existing) {
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Move-Item -LiteralPath $existing -Destination (Join-Path $backup $name)
        $moved += $name
    }
}
try {
    foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $target $name) -Recurse }
    foreach ($entry in $manifest.PSObject.Properties) {
        if ((Get-FileHash -LiteralPath (Join-Path $target $entry.Name) -Algorithm SHA256).Hash -ne $entry.Value) { throw "Installed checksum mismatch: $($entry.Name)" }
    }
} catch {
    # Preserve partial files for diagnosis rather than deleting user content.
    $failed = Join-Path $backup 'failed-install'
    New-Item -ItemType Directory -Force -Path $failed | Out-Null
    foreach ($name in $names) {
        $partial = [IO.Path]::GetFullPath((Join-Path $target $name))
        if (-not $partial.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe rollback path.' }
        if (Test-Path -LiteralPath $partial) { Move-Item -LiteralPath $partial -Destination (Join-Path $failed $name) }
    }
    foreach ($name in $moved) { Move-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $target $name) }
    throw
}
Write-Output "Installed: $target"
if ($moved.Count) { Write-Output "Backup: $backup" }
Write-Output 'Restart Houdini 21.0.440. F3 switches Chinese / English.'
