[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateRange(1, 8192)]
    [int]$Width = 480,

    [ValidateRange(1, 8192)]
    [int]$Height = 800,

    [ValidateRange(1, 8192)]
    [int]$StridePixels = 512
)

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($InputPath)
$destination = [IO.Path]::GetFullPath($OutputPath)
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "RGB565 framebuffer does not exist: $source"
}
if ($StridePixels -lt $Width) {
    throw "StridePixels ($StridePixels) must be at least Width ($Width)."
}

$raw = [IO.File]::ReadAllBytes($source)
$requiredBytes = $StridePixels * $Height * 2
if ($raw.Length -lt $requiredBytes) {
    throw "Framebuffer has $($raw.Length) bytes; expected at least $requiredBytes."
}

Add-Type -AssemblyName System.Drawing
$bitmap = [Drawing.Bitmap]::new(
    $Width, $Height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
$rect = [Drawing.Rectangle]::new(0, 0, $Width, $Height)
$bits = $bitmap.LockBits(
    $rect, [Drawing.Imaging.ImageLockMode]::WriteOnly,
    [Drawing.Imaging.PixelFormat]::Format24bppRgb)
try {
    $pixels = [byte[]]::new($bits.Stride * $Height)
    for ($y = 0; $y -lt $Height; $y++) {
        $srcRow = $y * $StridePixels * 2
        $dstRow = $y * $bits.Stride
        for ($x = 0; $x -lt $Width; $x++) {
            $src = $srcRow + $x * 2
            # Promote before shifting: a Byte left operand truncates the high
            # byte in PowerShell and falsely makes RGB565 captures look blue.
            $value = [int]$raw[$src] -bor ([int]$raw[$src + 1] -shl 8)
            $dst = $dstRow + $x * 3
            $pixels[$dst] = [byte]((($value -band 0x1f) * 255 + 15) / 31)
            $pixels[$dst + 1] = [byte](((($value -shr 5) -band 0x3f) * 255 + 31) / 63)
            $pixels[$dst + 2] = [byte](((($value -shr 11) -band 0x1f) * 255 + 15) / 31)
        }
    }
    [Runtime.InteropServices.Marshal]::Copy(
        $pixels, 0, $bits.Scan0, $pixels.Length)
}
finally {
    $bitmap.UnlockBits($bits)
}

$outputDirectory = Split-Path -Parent $destination
if ($outputDirectory) {
    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}
try {
    $bitmap.Save($destination, [Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $bitmap.Dispose()
}
Write-Host "Converted $source -> $destination ($Width x $Height, stride $StridePixels)"
