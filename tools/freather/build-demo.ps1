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

$scons = Join-Path $toolsRoot 'build-python\Scripts\scons.exe'
$toolchainBin = Join-Path $toolsRoot 'arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin'
if (-not (Test-Path -LiteralPath $scons -PathType Leaf)) {
    throw "External SCons is missing: $scons"
}
if (-not (Test-Path -LiteralPath (Join-Path $toolchainBin 'arm-none-eabi-gcc.exe') -PathType Leaf)) {
    throw "External Arm GNU toolchain is missing: $toolchainBin"
}

$oldRttExecPath = [Environment]::GetEnvironmentVariable('RTT_EXEC_PATH', 'Process')
$oldPythonIoEncoding = [Environment]::GetEnvironmentVariable('PYTHONIOENCODING', 'Process')
$oldPythonUtf8 = [Environment]::GetEnvironmentVariable('PYTHONUTF8', 'Process')

try {
    $env:RTT_EXEC_PATH = $toolchainBin
    # Edge Protect's Windows console output is GBK on this host. This keeps the
    # SDK SCons subprocess decoder from failing during signing/merge output.
    $env:PYTHONIOENCODING = 'gbk'
    $env:PYTHONUTF8 = '0'

    Push-Location -LiteralPath $projectPath
    try {
        if ($Clean) {
            & $scons -c
            if ($LASTEXITCODE -ne 0) {
                throw "SCons clean failed with exit code $LASTEXITCODE"
            }
        }

        & $scons "-j$Jobs"
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
$hash = Get-FileHash -LiteralPath $artifact -Algorithm SHA256
Write-Host ''
Write-Host 'Build completed.' -ForegroundColor Green
[PSCustomObject]@{
    Project  = $Project
    Artifact = $file.FullName
    Bytes    = $file.Length
    SHA256   = $hash.Hash
}
