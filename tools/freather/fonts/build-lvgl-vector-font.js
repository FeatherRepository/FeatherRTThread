#!/usr/bin/env node
'use strict';

/* Build the LVGL product font as canonical vector outlines.  Every size uses
 * the same path table; the target only changes metrics and a GPU matrix. */
const fs = require('fs');
const path = require('path');

const here = __dirname;
const root = path.resolve(here, '..', '..', '..');
/* The variable Google Fonts file defaults to wght=100 when loaded by the
 * pinned opentype.js converter.  That silently produced Thin outlines even
 * though the UI specification calls for a durable medium UI weight.  LVGL
 * already carries the matching official static Noto Sans SC Medium face, so
 * use that deterministic instance rather than relying on unsupported variable
 * font axis instantiation. */
const sourceFont = path.join(root, 'libraries', 'components', 'lvgl_9.2.0',
                             'demos', 'multilang', 'assets', 'fonts',
                             'NotoSansSC-Medium.otf');
const opentypePath = path.join(here, 'cache', 'node_modules', 'lv_font_conv',
                               'node_modules', 'opentype.js');
const uiRoot = path.join(root, 'projects', 'FeatherTalk_M55', 'applications', 'ui');
const output = path.join(uiRoot, 'assets', 'generated',
                         'feathertalk_vector_font_data.c');
const opentype = require(opentypePath);

if (!fs.existsSync(sourceFont)) {
  throw new Error('Official Noto Sans SC source is missing; run build-ui-fonts.ps1 first.');
}

function collectSources(directory) {
  const result = [];
  for (const name of fs.readdirSync(directory)) {
    if (name === 'assets') continue;
    const item = path.join(directory, name);
    const stat = fs.statSync(item);
    if (stat.isDirectory()) result.push(...collectSources(item));
    else if (/\.[ch]$/.test(name)) result.push(item);
  }
  return result;
}

const codepoints = new Set(Array.from({ length: 95 }, (_, index) => index + 32));
/* GB2312 punctuation/symbol zones and both simplified-Chinese levels. */
const gbDecoder = new TextDecoder('gb18030', { fatal: false });
for (const range of [[0xa1, 0xa9], [0xb0, 0xf7]]) {
  for (let lead = range[0]; lead <= range[1]; lead++) {
    for (let trail = 0xa1; trail <= 0xfe; trail++) {
      const decoded = gbDecoder.decode(Uint8Array.of(lead, trail));
      const chars = Array.from(decoded);
      if (chars.length === 1 && chars[0] !== '\ufffd') {
        const cp = chars[0].codePointAt(0);
        /* Windows' GB18030 decoder maps a number of otherwise unassigned
         * symbol-zone byte pairs into implementation-specific PUA slots.
         * They are neither GB2312 text nor product icons and Noto Sans SC
         * intentionally has no glyphs for them. */
        if (cp <= 0xffff && !(cp >= 0xe000 && cp <= 0xf8ff)) codepoints.add(cp);
      }
    }
  }
}
for (const file of collectSources(uiRoot)) {
  for (const character of Array.from(fs.readFileSync(file, 'utf8'))) {
    const cp = character.codePointAt(0);
    if (cp >= 32 && cp <= 0xffff) codepoints.add(cp);
  }
}

const font = opentype.loadSync(sourceFont);
if (!font.tables.os2 || font.tables.os2.usWeightClass !== 500) {
  throw new Error(`Expected Noto Sans SC Medium (weight 500), got ${
    font.tables.os2 ? font.tables.os2.usWeightClass : 'unknown'}`);
}
const glyphs = [];
const pathWords = [];
const missingCodepoints = [];

function appendPoint(x, y, bounds) {
  const px = Math.round(x - bounds.x1);
  const py = Math.round(bounds.y2 - y);
  if (px < -32768 || px > 32767 || py < -32768 || py > 32767) {
    throw new Error(`Vector coordinate outside int16: ${px},${py}`);
  }
  pathWords.push(px, py);
}

for (const codepoint of [...codepoints].sort((a, b) => a - b)) {
  const glyph = font.charToGlyph(String.fromCodePoint(codepoint));
  if (!glyph || glyph.index === 0) {
    missingCodepoints.push(codepoint);
    continue;
  }
  const bounds = glyph.getBoundingBox();
  const pathOffset = pathWords.length;
  for (const command of glyph.path.commands) {
    switch (command.type) {
      case 'M':
        pathWords.push(0x02); appendPoint(command.x, command.y, bounds); break;
      case 'L':
        pathWords.push(0x04); appendPoint(command.x, command.y, bounds); break;
      case 'Q':
        pathWords.push(0x06);
        appendPoint(command.x1, command.y1, bounds);
        appendPoint(command.x, command.y, bounds);
        break;
      case 'C':
        pathWords.push(0x08);
        appendPoint(command.x1, command.y1, bounds);
        appendPoint(command.x2, command.y2, bounds);
        appendPoint(command.x, command.y, bounds);
        break;
      case 'Z': pathWords.push(0x01); break;
      default: throw new Error(`Unsupported outline command ${command.type}`);
    }
  }
  const hasPath = pathWords.length !== pathOffset;
  if (hasPath) pathWords.push(0x00);
  glyphs.push({
    codepoint,
    pathOffset,
    pathWordCount: pathWords.length - pathOffset,
    x1: Math.round(bounds.x1),
    y1: Math.round(bounds.y1),
    x2: Math.round(bounds.x2),
    y2: Math.round(bounds.y2),
    advance: Math.round(glyph.advanceWidth || font.unitsPerEm)
  });
}

if (missingCodepoints.length !== 0) {
  throw new Error(`Noto Sans SC Medium is missing requested codepoints: ${
    missingCodepoints.map(cp => `U+${cp.toString(16).toUpperCase()}`).join(', ')}`);
}

function formatArray(values, formatter, width) {
  const lines = [];
  for (let index = 0; index < values.length; index += width) {
    lines.push('    ' + values.slice(index, index + width).map(formatter).join(', ') + ',');
  }
  return lines.join('\n');
}

const generated = `/* Generated by tools/freather/fonts/build-lvgl-vector-font.js.\n` +
` * Source: official Noto Sans SC Medium (weight 500); GB2312 plus the complete product UI subset. */\n` +
`#include \"feathertalk_ui_vector_font_data.h\"\n\n` +
`const uint16_t ft_vector_font_units_per_em = ${font.unitsPerEm}U;\n` +
`const int16_t ft_vector_font_typo_ascender = ${font.tables.os2.sTypoAscender};\n` +
`const int16_t ft_vector_font_typo_descender = ${font.tables.os2.sTypoDescender};\n` +
`const uint32_t ft_vector_font_glyph_count = ${glyphs.length}U;\n\n` +
`const ft_vector_font_glyph_asset_t ft_vector_font_glyphs[${glyphs.length}] = {\n` +
glyphs.map(glyph =>
`    { 0x${glyph.codepoint.toString(16).padStart(4, '0')}U, ${glyph.pathOffset}U, ` +
`${glyph.pathWordCount}U, ${glyph.x1}, ${glyph.y1}, ${glyph.x2}, ${glyph.y2}, ${glyph.advance}U },`).join('\n') +
`\n};\n\nconst int16_t ft_vector_font_path_data[${pathWords.length}] = {\n` +
formatArray(pathWords, value => String(value), 16) +
`\n};\n`;

const previous = fs.existsSync(output) ? fs.readFileSync(output, 'utf8') : '';
if (previous !== generated) {
  fs.writeFileSync(output, generated, 'utf8');
  console.log(`Generated LVGL vector font: ${glyphs.length} glyphs, ` +
              `${pathWords.length * 2} native S16 path bytes.`);
} else {
  console.log(`LVGL vector font is current: ${glyphs.length} glyphs.`);
}
