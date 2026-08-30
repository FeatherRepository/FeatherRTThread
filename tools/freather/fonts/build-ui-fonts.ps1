param(
    [switch]$RefreshSource
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir '..\..\..')).Path
$uiDir = Join-Path $repoRoot 'projects\FeatherTalk_M55\applications\ui'
$outputDir = Join-Path $uiDir 'assets\generated'
$cacheDir = Join-Path $scriptDir 'cache'
$fontPath = Join-Path $cacheDir 'NotoSansSC-wght.ttf'
$converter = Join-Path $cacheDir 'node_modules\lv_font_conv\lv_font_conv.js'
$fontUrl = 'https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf'
$fontSha256 = 'A3041811A78C361B1DE50F953C805E0244951C21C5BD412F7232EF0D899AF0DA'

New-Item -ItemType Directory -Force -Path $cacheDir, $outputDir | Out-Null
if ($RefreshSource -or -not (Test-Path -LiteralPath $fontPath)) {
    Invoke-WebRequest -Uri $fontUrl -OutFile $fontPath
}
$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fontPath).Hash
if ($actualHash -ne $fontSha256) {
    throw "Unexpected Noto Sans SC SHA-256: $actualHash"
}
if (-not (Test-Path -LiteralPath $converter)) {
    & npm.cmd install --prefix $cacheDir --no-save lv_font_conv@1.5.3
    if ($LASTEXITCODE -ne 0) { throw 'Unable to install lv_font_conv 1.5.3' }
}

$sourceText = Get-ChildItem -Path (Join-Path $uiDir '*') -File -Include '*.c','*.h' |
    Where-Object { $_.Name -notlike 'feathertalk_noto_sans_sc_*' } |
    ForEach-Object { Get-Content -Raw -Encoding UTF8 -LiteralPath $_.FullName }
$codepoints = [System.Collections.Generic.SortedSet[int]]::new()
foreach ($character in ($sourceText -join "`n").ToCharArray()) {
    $codepoint = [int]$character
    if (($codepoint -ge 0x3400 -and $codepoint -le 0x4DBF) -or
        ($codepoint -ge 0x4E00 -and $codepoint -le 0x9FFF) -or
        ($codepoint -ge 0x3000 -and $codepoint -le 0x303F) -or
        ($codepoint -ge 0xFF00 -and $codepoint -le 0xFFEF) -or
        $codepoint -eq 0x2026) {
        [void]$codepoints.Add($codepoint)
    }
}

# Product baseline: every GB2312 level-1/level-2 Han character (zones 16-87).
# This prevents runtime/device text that is not a literal in the UI sources from
# turning into tofu boxes, while remaining far smaller than the complete CJK
# Unified Ideographs block.
[Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
$gb2312 = [Text.Encoding]::GetEncoding(936,
    [Text.EncoderExceptionFallback]::new(),
    [Text.DecoderExceptionFallback]::new())
for ($lead = 0xB0; $lead -le 0xF7; $lead++) {
    for ($trail = 0xA1; $trail -le 0xFE; $trail++) {
        try {
            $decoded = $gb2312.GetString([byte[]]@($lead, $trail))
            if ($decoded.Length -eq 1) {
                $codepoint = [int]$decoded[0]
                if ($codepoint -ge 0x3400 -and $codepoint -le 0x9FFF) {
                    [void]$codepoints.Add($codepoint)
                }
            }
        } catch [Text.DecoderFallbackException] {
            # Five unassigned GB2312 positions are intentionally skipped.
        }
    }
}
if ($codepoints.Count -lt 6763) { throw "Incomplete product font set: $($codepoints.Count) glyphs" }

$symbolArgument = -join ($codepoints | ForEach-Object { [char]$_ })
$fallbackBySize = @{ 12 = 'lv_font_montserrat_12'; 14 = 'lv_font_montserrat_14';
                     16 = 'lv_font_montserrat_16'; 22 = 'lv_font_montserrat_22' }

foreach ($size in 12,14,16,22) {
    $name = "feathertalk_noto_sans_sc_$size"
    $output = Join-Path $outputDir "$name.c"
    & node $converter --font $fontPath --symbols $symbolArgument --size $size --bpp 2 `
        --format lvgl --no-compress --no-kerning --lv-include lvgl.h `
        --lv-font-name $name --lv-fallback $fallbackBySize[$size] -o $output
    if ($LASTEXITCODE -ne 0) { throw "Font generation failed for ${size}px" }
    $generated = [IO.File]::ReadAllText($output)
    $generated = $generated.Replace($fontPath, 'NotoSansSC-wght.ttf')
    $generated = $generated.Replace($output, "assets/generated/$name.c")
    $generated = [regex]::Replace(
        $generated, '(?m)^ \* Opts:.*$',
        ' * Opts: Noto Sans SC; GB2312 + UI subset; 2 bpp; uncompressed')
    # lv_font_conv emits several empty lines before its final feature guard.
    # Normalize this so regenerated sources remain clean under git diff --check.
    $generated = [regex]::Replace(
        $generated, '};\r?\n(?:\r?\n){2,}#endif',
        "};`n`n#endif")
    $generated = $generated.TrimEnd([char[]]"`r`n") + "`n"
    [IO.File]::WriteAllText($output, $generated, [Text.UTF8Encoding]::new($false))
}

Write-Host "Generated $($codepoints.Count) GB2312 + UI Chinese glyphs at 12/14/16/22 px."
Write-Host "Source SHA-256: $actualHash"
