[CmdletBinding()]
param(
    [ValidateRange(100, 12000)]
    [int]$AdapterKHz = 4000,

    [string]$ProbeUid = '0D141868022E2400'
)

$ErrorActionPreference = 'Stop'

$toolsRoot = Split-Path -Parent $PSCommandPath
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $toolsRoot '..\..'))
$flashDemo = Join-Path $toolsRoot 'flash-demo.ps1'
$m55Image = Join-Path $sdkRoot 'projects\FeatherTalk_M55\rtthread.hex'
$m33Image = Join-Path $sdkRoot 'projects\FeatherTalk_M33\build\rtthread.hex'

foreach ($required in @($flashDemo, $m55Image, $m33Image)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required FeatherTalk programming input is missing: $required"
    }
}

Write-Host 'Step 1/2: programming the M55 XIP image at 0x60580400...'
& $flashDemo `
    -Project 'FeatherTalk_M55' `
    -Programmer 'OpenOCD' `
    -Image $m55Image `
    -AdapterKHz $AdapterKHz `
    -ProbeUid $ProbeUid

Write-Host 'Step 2/2: programming the signed Secure/M33 image and restarting the board...'
& $flashDemo `
    -Project 'FeatherTalk_M33' `
    -Programmer 'OpenOCD' `
    -Image $m33Image `
    -AdapterKHz $AdapterKHz `
    -ProbeUid $ProbeUid

Write-Host 'FeatherTalk M55 and M33 images programmed and verified.' -ForegroundColor Green
