[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$separator = [IO.Path]::DirectorySeparatorChar
$projectPath = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd($separator)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd($separator)
$buildPath = Join-Path $projectPath 'build'
$markerName = '.houdini_chinese_build_output'
$markerText = 'HoudiniChinese generated build output. Safe to regenerate.'

function Test-Within([string]$Path, [string]$Directory) {
    return $Path.StartsWith($Directory + $separator, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparsePoint([string]$Path) {
    $currentPath = $Path
    while ($currentPath) {
        if (Test-Path -LiteralPath $currentPath) {
            $item = Get-Item -LiteralPath $currentPath -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Build output may not traverse a link or junction: $currentPath"
            }
        }
        $parentPath = Split-Path -Parent $currentPath
        if ($parentPath -eq $currentPath) { break }
        $currentPath = $parentPath
    }
}

# Reject source directories, ancestors and volume roots before creating or
# deleting anything. Within the repository, generated output belongs in build/.
if ($outputPath -eq [IO.Path]::GetPathRoot($outputPath).TrimEnd($separator) -or
    -not (Split-Path -Leaf $outputPath) -or
    $outputPath -eq $projectPath -or (Test-Within $projectPath $outputPath) -or
    ($outputPath -eq $buildPath) -or
    ((Test-Within $outputPath $projectPath) -and -not (Test-Within $outputPath $buildPath))) {
    throw "Unsafe build output directory: $outputPath"
}
Assert-NoReparsePoint $outputPath

$markerPath = Join-Path $outputPath $markerName
if (Test-Path -LiteralPath $outputPath) {
    if (-not (Test-Path -LiteralPath $outputPath -PathType Container)) {
        throw "Build output is not a directory: $outputPath"
    }
    if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        Assert-NoReparsePoint $markerPath
        if ((Get-Content -LiteralPath $markerPath -Raw).Trim() -ne $markerText) {
            throw "Invalid build ownership marker: $markerPath"
        }
    } elseif (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1) {
        throw "Existing output is not owned by this build: $outputPath"
    }
} else {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

$targets = @('obj', 'package', 'HoudiniChineseHook.dll',
             'HoudiniChineseHook.exp', 'HoudiniChineseHook.lib') |
    ForEach-Object { [IO.Path]::GetFullPath((Join-Path $outputPath $_)) }
# Validate every target and descendant first; a later validation failure must
# not leave a partly cleaned output. Never follow directory junctions.
foreach ($targetPath in $targets) {
    if (-not (Test-Within $targetPath $outputPath)) {
        throw "Generated target escaped the build output: $targetPath"
    }
    Assert-NoReparsePoint $targetPath
    if (Test-Path -LiteralPath $targetPath -PathType Container) {
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($targetPath)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Generated output contains a link or junction: $($item.FullName)"
                }
                if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            }
        }
    }
}
foreach ($targetPath in $targets) {
    if (Test-Path -LiteralPath $targetPath) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
    }
}
Set-Content -LiteralPath $markerPath -Value $markerText -Encoding Ascii
New-Item -ItemType Directory -Path (Join-Path $outputPath 'obj\detours') | Out-Null
