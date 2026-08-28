[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Project = 'Edgi_Talk_M33_Template',

    [ValidateSet('Auto', 'OpenOCD', 'PyOCD')]
    [string]$Programmer = 'Auto',

    [string]$Image,

    [ValidateRange(100, 12000)]
    [int]$AdapterKHz = 4000,

    [string]$ProbeUid = '0D141868022E2400'
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

if ([string]::IsNullOrWhiteSpace($Image)) {
    $Image = Join-Path $projectPath 'build\rtthread.hex'
}
else {
    $Image = [IO.Path]::GetFullPath($Image)
}
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw "Final merged firmware image not found: $Image. Run tools\freather\build-demo.cmd first."
}

$pack = Join-Path $toolsRoot 'cmsis-packs\Infineon.PSE8xxx_DFP.1.1.0.pack'
$pyocd = Join-Path $toolsRoot 'pyocd.cmd'
$openocd = Join-Path $toolsRoot 'openocd.cmd'
$openocdScripts = Join-Path $toolsRoot 'openocd\scripts'
$openocdFlm = Join-Path $toolsRoot 'openocd\flm\infineon\pse8x6'
$qspiConfigDir = Join-Path $projectsRoot 'libs\TARGET_APP_KIT_PSE84_EVAL_EPC2\config\GeneratedSource'

if ($Programmer -eq 'PyOCD') {
    if (-not (Test-Path -LiteralPath $pack -PathType Leaf)) {
        throw "PSE84 CMSIS Pack is missing: $pack"
    }

    Write-Host 'Running the safe PyOCD PSE84 connection check (auto-unlock disabled)...'
    $output = & $pyocd commander `
        --pack $pack `
        --target pse846gps2dbzc4a `
        --uid $ProbeUid `
        --frequency 1MHz `
        --connect under-reset `
        -O auto_unlock=false `
        -O pack.debug_sequences.enable=true `
        -O primary_core=0 `
        --command 'show cores' `
        --command 'show map' 2>&1
    $output | ForEach-Object { Write-Host $_ }

    throw @'
PyOCD programming is intentionally blocked for this image on the current setup.
PyOCD 0.45.1 can parse Infineon.PSE8xxx_DFP 1.1.0, but generic CMSIS-DAP
cannot perform KitProg3's proprietary PSE84 acquire command. In addition, the
Pack marks PSE84_SMIF.FLM as non-default, so PyOCD does not create the external
SMIF flash region used by build\rtthread.hex. Use -Programmer OpenOCD (or Auto).
'@
}

if ($Programmer -eq 'Auto') {
    Write-Warning 'PyOCD PSE84/KitProg3 acquire and external SMIF programming are not safe on this setup; selecting Infineon OpenOCD.'
}

foreach ($required in @($openocd, $openocdScripts, $openocdFlm, $qspiConfigDir)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required programming resource is missing: $required"
    }
}

$tclImage = $Image.Replace('\', '/')
$openocdArgs = @(
    '-s', $openocdScripts,
    '-s', $openocdFlm,
    '-s', $qspiConfigDir,
    '-f', 'interface/kitprog3.cfg',
    '-f', 'target/infineon/pse84xgxs2.cfg',
    '-c', "adapter serial $ProbeUid",
    '-c', 'transport select swd',
    '-c', "adapter speed $AdapterKHz",
    '-c', 'init',
    # The generic `reset init` path re-runs Test-Mode acquisition and removes
    # the SMIF banks when the board is already in normal DEVELOPMENT mode.
    # PSE84's target-specific helper halts at the secure reset handler while
    # preserving the board-generated external flash map.
    '-c', 'reset_halt cm33',
    '-c', 'targets cat1d.cm33',
    '-c', "flash write_image erase {$tclImage}",
    '-c', "verify_image {$tclImage}",
    '-c', 'reset_halt cm33_ns',
    '-c', 'resume',
    '-c', 'shutdown'
)

Write-Host "Programming final merged image: $Image"
& $openocd @openocdArgs
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD programming failed with exit code $LASTEXITCODE"
}

Write-Host 'Programming and verification completed.' -ForegroundColor Green
