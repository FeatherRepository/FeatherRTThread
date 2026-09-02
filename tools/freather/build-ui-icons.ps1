[CmdletBinding()]
param(
    [ValidateRange(1, 8)]
    [int]$Samples = 4
)

$ErrorActionPreference = 'Stop'

$toolsRoot = Split-Path -Parent $PSCommandPath
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $toolsRoot '..\..'))
$python = Join-Path $toolsRoot 'serial-monitor\python\python.exe'
$converter = Join-Path $toolsRoot 'ui-svg-icon-convert.py'
$uiRoot = Join-Path $sdkRoot 'projects\FeatherTalk_M55\applications\ui'
$sourceRoot = Join-Path $uiRoot 'assets-src'
$outputRoot = Join-Path $uiRoot 'assets\generated'
$outputC = Join-Path $outputRoot 'feathertalk_icon_assets.c'
$outputH = Join-Path $outputRoot 'feathertalk_icon_assets.h'
$vectorOutputC = Join-Path $outputRoot 'feathertalk_icon_vector_assets.c'
$vectorOutputH = Join-Path $outputRoot 'feathertalk_icon_vector_assets.h'

if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw "External build Python is missing: $python"
}
if (-not (Test-Path -LiteralPath $converter -PathType Leaf)) {
    throw "SVG converter is missing: $converter"
}
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "SVG source directory is missing: $sourceRoot"
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
& $python $converter $sourceRoot $outputC $outputH --sizes '24,32,48' --samples $Samples `
    --vector-output-c $vectorOutputC --vector-output-h $vectorOutputH
if ($LASTEXITCODE -ne 0) {
    throw "SVG icon generation failed with exit code $LASTEXITCODE"
}

Write-Host ''
Write-Host 'UI icons generated.' -ForegroundColor Green
Get-Item -LiteralPath $outputC, $outputH, $vectorOutputC, $vectorOutputH |
    Select-Object FullName, Length, LastWriteTime
