$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$payload = Join-Path $root 'release\HoudiniChinese-21.0.440'
$script = Join-Path $root 'installer\HoudiniChineseInstaller.iss'
$output = Join-Path $root 'release\HoudiniChineseInstaller-21.0.440.exe'

if (-not (Test-Path -LiteralPath (Join-Path $payload 'HoudiniChinese\HoudiniChineseHook.dll'))) {
    $staged = Join-Path $root 'build\houdini21\package\HoudiniChinese\HoudiniChineseHook.dll'
    if (-not (Test-Path -LiteralPath $staged)) {
        throw '缺少已构建的 Houdini 21.0.440 包。请先运行 build21.cmd。'
    }
    & python -X utf8 (Join-Path $root 'tools\package21.py')
    if ($LASTEXITCODE -ne 0) { throw '生成发行包失败。' }
}

$candidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
)
$compiler = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $compiler) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $compiler = $command.Source }
}
if (-not $compiler) {
    throw '未找到 Inno Setup 6。可运行：winget install --id JRSoftware.InnoSetup --exact'
}

& $compiler $script
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup 编译失败。' }
if (-not (Test-Path -LiteralPath $output)) { throw '安装器输出文件不存在。' }

$stream = [IO.File]::OpenRead($output)
try {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha256.Dispose() }
}
finally { $stream.Dispose() }
$hashFile = Join-Path $root 'release\HoudiniChineseInstaller-21.0.440.sha256.txt'
Set-Content -LiteralPath $hashFile -Encoding ascii -NoNewline -Value "$hash  HoudiniChineseInstaller-21.0.440.exe`n"
Get-Item -LiteralPath $output | Select-Object FullName, Length, LastWriteTime
Write-Output "SHA256: $hash"
