#!/usr/bin/env node
'use strict';

/* Generate the product-specific FeatherUI ASCII/CJK A8 atlas.  The source
 * font and lv_font_conv stay in the ignored cache; only deterministic C data
 * is checked into the firmware tree. */
const fs = require('fs');
const path = require('path');
const child = require('child_process');

const here = __dirname;
const root = path.resolve(here, '..', '..', '..');
const cache = path.join(here, 'cache');
const font = path.join(cache, 'NotoSansSC-wght.ttf');
const converter = path.join(cache, 'node_modules', 'lv_font_conv', 'lv_font_conv.js');
const PNG = require(path.join(cache, 'node_modules', 'lv_font_conv',
                              'node_modules', 'pngjs')).PNG;
const scene = path.join(root, 'projects', 'FeatherTalk_M55', 'applications',
                        'gpu_ui', 'feathertalk_gpu_scene.c');
const productOutput = path.join(root, 'libraries', 'FeatherUI', 'src',
                               'fui_font_product_data.c');

if (!fs.existsSync(font) || !fs.existsSync(converter)) {
  throw new Error('Run build-ui-fonts.ps1 once to provision the official Noto Sans SC source.');
}
const source = fs.readFileSync(scene, 'utf8');
const codepoints = [...new Set(Array.from(source)
  .map(character => character.codePointAt(0))
  .filter(codepoint => codepoint > 0x7f && codepoint <= 0xffff))]
  .sort((left, right) => left - right);
if (codepoints.length === 0) throw new Error('No non-ASCII UI characters found.');

function cArray(bytes) {
  const lines = [];
  for (let offset = 0; offset < bytes.length; offset += 16) {
    lines.push('    ' + Array.from(bytes.subarray(offset, offset + 16))
      .map(value => '0x' + value.toString(16).padStart(2, '0')).join(', ') + ',');
  }
  return lines.join('\n');
}
function codeArray(values) {
  const lines = [];
  for (let offset = 0; offset < values.length; offset += 8) {
    lines.push('    ' + values.slice(offset, offset + 8)
      .map(value => '0x' + value.toString(16).padStart(4, '0') + 'U').join(', ') + ',');
  }
  return lines.join('\n');
}

/* FeatherUI text scale is a semantic size, not a bitmap zoom factor.  Keep
 * one antialiased source atlas in scarce GFX memory and let the GPU use linear
 * sampling for the three semantic sizes.  ASCII and CJK share metrics. */
const productCodes = [...new Set([
  ...Array.from({length: 95}, (_, index) => index + 32), ...codepoints
])].sort((left, right) => left - right);
const productSymbols = String.fromCodePoint(...productCodes);
const styles = [
  { size: 14, cell: 18 }
];
const productAtlases = [];
for (let styleIndex = 0; styleIndex < styles.length; styleIndex++) {
  const style = styles[styleIndex];
  const styleDump = path.join(cache, `featherui-product-${styleIndex + 1}`);
  fs.rmSync(styleDump, { recursive: true, force: true });
  fs.mkdirSync(styleDump, { recursive: true });
  const conversion = child.spawnSync(process.execPath, [converter, '--font', font,
    '--symbols', productSymbols, '--size', String(style.size), '--bpp', '8',
    '--no-compress', '--no-prefilter', '--no-kerning', '--format', 'dump',
    '-o', styleDump], { stdio: 'inherit' });
  if (conversion.status !== 0) throw new Error(`product font style ${styleIndex + 1} failed`);
  const info = JSON.parse(fs.readFileSync(path.join(styleDump, 'font_info.json'), 'utf8'));
  const metrics = new Map(info.glyphs.map(glyph => [glyph.code, glyph]));
  const productColumns = 16;
  const productStride = productColumns * style.cell;
  const productRows = Math.ceil(productCodes.length / productColumns);
  const bytes = Buffer.alloc(productStride * productRows * style.cell);
  const advances = [];
  for (let index = 0; index < productCodes.length; index++) {
    const codepoint = productCodes[index];
    const metric = metrics.get(codepoint);
    const advance = metric ? Math.max(1, Math.min(255,
      Math.round(metric.freetype.metrics.horiAdvance))) :
      Math.max(1, Math.round(style.size / 3));
    advances.push(advance);
    if (codepoint === 32) continue;
    const imagePath = path.join(styleDump, codepoint.toString(16) + '.png');
    if (!fs.existsSync(imagePath)) continue;
    const image = PNG.sync.read(fs.readFileSync(imagePath));
    /* lv_font_conv dump PNGs already contain their baseline/bearing padding. */
    const offsetX = 0;
    const offsetY = 0;
    if (offsetX + image.width > style.cell || offsetY + image.height > style.cell)
      throw new Error(`Glyph U+${codepoint.toString(16)} exceeds ${style.cell}px cell at ${style.size}px`);
    const originX = (index % productColumns) * style.cell + offsetX;
    const originY = Math.floor(index / productColumns) * style.cell + offsetY;
    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        const pixel = (y * image.width + x) * 4;
        bytes[(originY + y) * productStride + originX + x] = 255 - image.data[pixel];
      }
    }
  }
  productAtlases.push({ style, stride: productStride,
                        height: productRows * style.cell, bytes, advances });
}
const productArrays = productAtlases.map((item, index) =>
`#define FUI_PRODUCT_CELL_${index + 1} ${item.style.cell}U\n` +
`#define FUI_PRODUCT_STRIDE_${index + 1} ${item.stride}U\n` +
`#define FUI_PRODUCT_HEIGHT_${index + 1} ${item.height}U\n` +
`static const uint8_t s_product_source_${index + 1}[FUI_PRODUCT_STRIDE_${index + 1} * FUI_PRODUCT_HEIGHT_${index + 1}] = {\n${cArray(item.bytes)}\n};\n` +
`static const uint8_t s_product_advance_${index + 1}[FUI_PRODUCT_GLYPH_COUNT] = {\n${cArray(Buffer.from(item.advances))}\n};\n` +
`CY_SECTION(".cy_gpu_buf") CY_ALIGN(32)\nstatic uint8_t s_product_pixels_${index + 1}[FUI_PRODUCT_STRIDE_${index + 1} * FUI_PRODUCT_HEIGHT_${index + 1}];\n` +
`static vg_lite_buffer_t s_product_atlas_${index + 1};\n`).join('\n');
const productInitCopies = productAtlases.map((item, index) => {
  const id = index + 1;
  return `    memcpy(s_product_pixels_${id}, s_product_source_${id}, sizeof(s_product_pixels_${id}));\n` +
`    memset(&s_product_atlas_${id}, 0, sizeof(s_product_atlas_${id}));\n` +
`    s_product_atlas_${id}.width = FUI_PRODUCT_STRIDE_${id};\n` +
`    s_product_atlas_${id}.height = FUI_PRODUCT_HEIGHT_${id};\n` +
`    s_product_atlas_${id}.stride = FUI_PRODUCT_STRIDE_${id};\n` +
`    s_product_atlas_${id}.format = VG_LITE_A8;\n` +
`    s_product_atlas_${id}.tiled = VG_LITE_LINEAR;\n` +
`    s_product_atlas_${id}.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;\n` +
`    s_product_atlas_${id}.transparency_mode = VG_LITE_IMAGE_TRANSPARENT;\n` +
`    s_product_atlas_${id}.memory = s_product_pixels_${id};\n` +
`    s_product_atlas_${id}.address = (uint32_t)(uintptr_t)s_product_pixels_${id};`;
}).join('\n');
const productGenerated = `/* Generated by tools/freather/fonts/build-featherui-font.js.\n` +
` * Source: official Noto Sans SC; one product atlas with semantic GPU scaling; A8. */\n` +
`#include <string.h>\n#include <board.h>\n#include "fui_internal.h"\n\n` +
`#define FUI_PRODUCT_GLYPH_COUNT ${productCodes.length}U\n#define FUI_PRODUCT_COLUMNS 16U\n\n` +
`static const uint32_t s_product_codes[FUI_PRODUCT_GLYPH_COUNT] = {\n${codeArray(productCodes)}\n};\n\n` +
productArrays + `\nstatic bool s_product_ready;\n\nstatic void product_font_init(void)\n{\n` +
`    if (s_product_ready) return;\n${productInitCopies}\n    s_product_ready = true;\n}\n\n` +
`uint16_t fui_font_scale_dimension(uint16_t base, uint8_t text_scale)\n{\n` +
`    if (text_scale >= 3U) return (uint16_t)((base * 3U + 1U) / 2U);\n` +
`    if (text_scale == 2U) return base;\n` +
`    return (uint16_t)((base * 3U + 2U) / 4U);\n}\n\n` +
`uint8_t fui_font_line_height(uint8_t text_scale)\n{\n` +
`    return (uint8_t)fui_font_scale_dimension(FUI_PRODUCT_CELL_1, text_scale);\n}\n\n` +
`bool fui_font_glyph_get(uint32_t codepoint, uint8_t text_scale, fui_font_glyph_t *glyph)\n{\n` +
`    uint32_t low = 0U;\n    uint32_t high = FUI_PRODUCT_GLYPH_COUNT;\n` +
`    uint32_t index;\n    uint8_t cell;\n` +
`    if (glyph == RT_NULL) return false;\n    while (low < high)\n    {\n` +
`        uint32_t middle = low + (high - low) / 2U;\n` +
`        if (s_product_codes[middle] < codepoint) low = middle + 1U;\n        else high = middle;\n    }\n` +
`    if (low >= FUI_PRODUCT_GLYPH_COUNT || s_product_codes[low] != codepoint)\n` +
`    {\n        codepoint = (uint32_t)'?';\n        low = 0U;\n        high = FUI_PRODUCT_GLYPH_COUNT;\n` +
`        while (low < high)\n        {\n            uint32_t middle = low + (high - low) / 2U;\n` +
`            if (s_product_codes[middle] < codepoint) low = middle + 1U;\n            else high = middle;\n        }\n` +
`        if (low >= FUI_PRODUCT_GLYPH_COUNT || s_product_codes[low] != codepoint) return false;\n    }\n` +
`    index = low;\n    product_font_init();\n` +
`    (void)text_scale;\n    cell = FUI_PRODUCT_CELL_1;\n` +
`    glyph->atlas = &s_product_atlas_1;\n    glyph->advance = s_product_advance_1[index];\n` +
`    glyph->source_x = (uint16_t)((index % FUI_PRODUCT_COLUMNS) * cell);\n` +
`    glyph->source_y = (uint16_t)((index / FUI_PRODUCT_COLUMNS) * cell);\n` +
`    glyph->width = cell;\n    glyph->height = cell;\n    return true;\n}\n`;
const previousProduct = fs.existsSync(productOutput) ? fs.readFileSync(productOutput, 'utf8') : '';
if (previousProduct !== productGenerated) {
  fs.writeFileSync(productOutput, productGenerated, 'utf8');
  console.log(`Generated native FeatherUI product font: ${productCodes.length} glyphs, ` +
              `${productAtlases.reduce((sum, item) => sum + item.bytes.length, 0)} A8 bytes.`);
} else {
  console.log(`Native FeatherUI product font is current: ${productCodes.length} glyphs.`);
}
