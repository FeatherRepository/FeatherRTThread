[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Project = 'Edgi_Talk_M33_Template',

    [ValidateRange(1, 64)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$toolsRoot = Split-Path -Parent $PSCommandPath
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $toolsRoot '..\..'))
$projectsRoot = Join-Path $sdkRoot 'projects'
$projectPath = [IO.Path]::GetFullPath((Join-Path $projectsRoot $Project))
$projectsPrefix = [IO.Path]::GetFullPath($projectsRoot).TrimEnd('\') + '\'

if (-not $projectPath.StartsWith($projectsPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Project must stay under $projectsRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $projectPath 'SConstruct') -PathType Leaf)) {
    throw "SCons project not found: $projectPath"
}

$buildPython = Join-Path $toolsRoot 'serial-monitor\python\python.exe'
$sconsPackages = Join-Path $toolsRoot 'build-python\Lib\site-packages'
$toolchainBin = Join-Path $toolsRoot 'arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin'
if (-not (Test-Path -LiteralPath $buildPython -PathType Leaf)) {
    throw "External build Python is missing: $buildPython"
}
if (-not (Test-Path -LiteralPath (Join-Path $sconsPackages 'SCons') -PathType Container)) {
    throw "External SCons package is missing: $sconsPackages"
}
if (-not (Test-Path -LiteralPath (Join-Path $toolchainBin 'arm-none-eabi-gcc.exe') -PathType Leaf)) {
    throw "External Arm GNU toolchain is missing: $toolchainBin"
}

if ($Project -eq 'FeatherTalk_M55') {
    $projectConfig = Get-Content -LiteralPath (Join-Path $projectPath '.config') -Raw
    $fontGeneratorName = if ($projectConfig -match '(?m)^CONFIG_FEATHERTALK_USING_UI_SHELL=y$') {
        'build-lvgl-vector-font.js'
    }
    else {
        'build-featherui-font.js'
    }
    $fontGenerator = Join-Path $toolsRoot (Join-Path 'fonts' $fontGeneratorName)
    $fontSource = if ($fontGeneratorName -eq 'build-lvgl-vector-font.js') {
        Join-Path $sdkRoot 'libraries\components\lvgl_9.2.0\demos\multilang\assets\fonts\NotoSansSC-Medium.otf'
    }
    else {
        Join-Path $toolsRoot 'fonts\cache\NotoSansSC-wght.ttf'
    }
    $fontConverter = Join-Path $toolsRoot 'fonts\cache\node_modules\lv_font_conv\lv_font_conv.js'
    $node = Get-Command node -ErrorAction SilentlyContinue
    if ($node -and
        (Test-Path -LiteralPath $fontGenerator -PathType Leaf) -and
        (Test-Path -LiteralPath $fontSource -PathType Leaf) -and
        (Test-Path -LiteralPath $fontConverter -PathType Leaf)) {
        & $node.Source $fontGenerator
        if ($LASTEXITCODE -ne 0) {
            throw "UI font generation failed with exit code $LASTEXITCODE"
        }
    }
    else {
        Write-Warning 'UI font tools are not provisioned; using the checked-in generated font assets.'
    }
}

$oldRttExecPath = [Environment]::GetEnvironmentVariable('RTT_EXEC_PATH', 'Process')
$oldPythonIoEncoding = [Environment]::GetEnvironmentVariable('PYTHONIOENCODING', 'Process')
$oldPythonUtf8 = [Environment]::GetEnvironmentVariable('PYTHONUTF8', 'Process')
$oldPythonPath = [Environment]::GetEnvironmentVariable('PYTHONPATH', 'Process')

try {
    $env:RTT_EXEC_PATH = $toolchainBin
    # Edge Protect's Windows console output is GBK on this host. This keeps the
    # SDK SCons subprocess decoder from failing during signing/merge output.
    $env:PYTHONIOENCODING = 'gbk'
    $env:PYTHONUTF8 = '0'
    $env:PYTHONPATH = $sconsPackages

    Push-Location -LiteralPath $projectPath
    try {
        if ($Clean) {
            & $buildPython -m SCons -c
            if ($LASTEXITCODE -ne 0) {
                throw "SCons clean failed with exit code $LASTEXITCODE"
            }
        }

        & $buildPython -m SCons "-j$Jobs"
        if ($LASTEXITCODE -ne 0) {
            throw "SCons build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    if ($null -eq $oldRttExecPath) { Remove-Item Env:RTT_EXEC_PATH -ErrorAction SilentlyContinue } else { $env:RTT_EXEC_PATH = $oldRttExecPath }
    if ($null -eq $oldPythonIoEncoding) { Remove-Item Env:PYTHONIOENCODING -ErrorAction SilentlyContinue } else { $env:PYTHONIOENCODING = $oldPythonIoEncoding }
    if ($null -eq $oldPythonUtf8) { Remove-Item Env:PYTHONUTF8 -ErrorAction SilentlyContinue } else { $env:PYTHONUTF8 = $oldPythonUtf8 }
    if ($null -eq $oldPythonPath) { Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue } else { $env:PYTHONPATH = $oldPythonPath }
}

$artifactCandidates = @(
    (Join-Path $projectPath 'build\rtthread.hex'),
    (Join-Path $projectPath 'rtthread.hex')
)
$artifact = $artifactCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $artifact) {
    throw "Build completed but no firmware HEX was found under $projectPath"
}

$file = Get-Item -LiteralPath $artifact
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $stream = [IO.File]::OpenRead($artifact)
    try {
        $hash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '')
    }
    finally {
        $stream.Dispose()
    }
}
finally {
    $sha256.Dispose()
}
Write-Host ''
Write-Host 'Build completed.' -ForegroundColor Green
[PSCustomObject]@{
    Project  = $Project
    Artifact = $file.FullName
    Bytes    = $file.Length
    SHA256   = $hash
}
