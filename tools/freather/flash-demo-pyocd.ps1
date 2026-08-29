[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Project = 'Edgi_Talk_M33_Template',

    [string]$Image,

    [string]$ProbeUid = '1C180A11022F2400',

    [ValidateRange(100, 10000)]
    [int]$AdapterKHz = 1000,

    [int]$PrewarmTimeoutSec = 15
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

$openocd = Join-Path $toolsRoot 'openocd.cmd'
$openocdScripts = Join-Path $toolsRoot 'openocd\scripts'
$openocdFlm = Join-Path $toolsRoot 'openocd\flm\infineon\pse8x6'
$qspiConfigDir = Join-Path $projectsRoot 'libs\TARGET_APP_KIT_PSE84_EVAL_EPC2\config\GeneratedSource'
$pyocdLib = Join-Path $toolsRoot 'pyocd-lib'
$pyocdDriver = Join-Path $toolsRoot 'pyocd_flash_pse84.py'
$pyocdPack = Join-Path $toolsRoot 'cmsis-packs\Infineon.PSE8xxx_DFP.1.1.0-smif-default.pack'
$bundledPython = Join-Path $toolsRoot 'serial-monitor\python\python.exe'

foreach ($required in @($openocd, $openocdScripts, $pyocdLib, $pyocdDriver, $pyocdPack, $bundledPython)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required resource is missing: $required"
    }
}

# ---------------------------------------------------------------------------
# Step 1: OpenOCD pre-warm.
#
# The KitProg3 firmware performs the PSE84 SWD bring-up with a custom acquire
# sequence that only the kitprog3 driver can trigger. On a freshly powered
# board pyOCD's DFP DebugPortSetup cannot wake the gated SWD DP (No ACK).
# Running OpenOCD through `init; reset halt; resume` leaves the debug domain
# powered, so pyOCD can take over. OpenOCD must be killed WITHOUT `shutdown`:
# a clean shutdown powers the debug domain back down.
# ---------------------------------------------------------------------------
Write-Host "Step 1/2: OpenOCD pre-warm (acquire + reset halt + resume)..."
$ocdLog = Join-Path $env:TEMP ('kitprog3_prewarm_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
$ocdArgs = @(
    '-s', $openocdScripts,
    '-s', $openocdFlm,
    '-s', $qspiConfigDir,
    '-f', 'interface/kitprog3.cfg',
    '-f', 'target/infineon/pse84xgxs2.cfg',
    '-c', "adapter serial $ProbeUid",
    '-c', 'transport select swd',
    '-c', "adapter speed $AdapterKHz",
    '-c', 'init',
    '-c', 'reset halt',
    '-c', 'resume',
    '-c', "sleep 60000"
)
$ocdProc = Start-Process -FilePath (Join-Path $toolsRoot 'openocd\bin\openocd.exe') `
    -ArgumentList ($ocdArgs | ForEach-Object { '"' + $_ + '"' }) `
    -RedirectStandardError $ocdLog -RedirectStandardOutput ($ocdLog + '.out') `
    -PassThru -NoNewWindow

$deadline = (Get-Date).AddSeconds($PrewarmTimeoutSec)
$warmed = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 300
    if (Test-Path $ocdLog) {
        $txt = Get-Content $ocdLog -Raw -ErrorAction SilentlyContinue
        if ($txt -match 'halted at|halted due to|resume') { $warmed = $true; break }
    }
    if ($ocdProc.HasExited) { break }
}
if (-not $ocdProc.HasExited) {
    Stop-Process -Id $ocdProc.Id -Force
}
Start-Sleep -Milliseconds 500

if ($warmed) {
    Write-Host '  OpenOCD pre-warm complete (debug domain powered, probe released).' -ForegroundColor Green
}
else {
    Write-Warning '  OpenOCD pre-warm may not have completed; continuing (pyOCD will report if the target is unreachable).'
}

# ---------------------------------------------------------------------------
# Step 2: pyOCD program + verify + reset.
# ---------------------------------------------------------------------------
Write-Host 'Step 2/2: pyOCD programming...'
$env:PYTHONPATH = $pyocdLib
$env:PYTHONNOUSERSITE = '1'
$env:PYTHONUTF8 = '1'
& $bundledPython -u $pyocdDriver $Image `
    --pack $pyocdPack `
    --uid $ProbeUid `
    --frequency ($AdapterKHz * 1000) @args
if ($LASTEXITCODE -ne 0) {
    throw "pyOCD programming failed with exit code $LASTEXITCODE"
}

Write-Host 'Programming and verification completed (pyOCD).' -ForegroundColor Green
