'use strict';
/*
 * Item icons for the item manager: cuts one 32x32 cell out of the game's .rttex textures
 * and sends it as a PNG. The textures are Growtopia's own game folder (TEXTURES_DIR),
 * the same files the game server's /renderworld uses.
 */
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const CELL = 32;
const MAX_SHEETS = 12; // @note a decoded sheet is up to a few MB
const MAX_ICONS = 4000;

const sheets = new Map(); // @note file -> { w, h, rgba } or null when it can't be read
const icons = new Map();  // @note "file:x:y" -> PNG buffer

/* @note .rttex: an optional RTPACK (zlib) around an RTTXTR header and raw pixels, bottom row first */
function decode(buf) {
  let data = buf;
  if (data.length >= 32 && data.toString('latin1', 0, 6) === 'RTPACK') {
    if (data[16] !== 1) return null;
    data = zlib.inflateSync(data.subarray(32));
  }
  if (data.length < 124 || data.toString('latin1', 0, 6) !== 'RTTXTR') return null;
  const height = data.readInt32LE(8);
  const width = data.readInt32LE(12);
  const format = data.readInt32LE(16);
  const alpha = data[28] !== 0;
  const compressed = data[29] !== 0;
  if (format !== 0x1401 || compressed || width <= 0 || height <= 0 || width > 8192 || height > 8192) return null;
  const channels = alpha ? 4 : 3;
  if (data.length < 124 + width * height * channels) return null;
  const rgba = Buffer.alloc(width * height * 4);
  for (let y = 0; y < height; y++) {
    const from = 124 + (height - 1 - y) * width * channels; // @note flip: the file starts at the bottom row
    for (let x = 0; x < width; x++) {
      const s = from + x * channels;
      const d = (y * width + x) * 4;
      rgba[d] = data[s]; rgba[d + 1] = data[s + 1]; rgba[d + 2] = data[s + 2];
      rgba[d + 3] = alpha ? data[s + 3] : 255;
    }
  }
  return { w: width, h: height, rgba };
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, body) {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(body.length, 0);
  head.write(type, 4, 'latin1');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])), 0);
  return Buffer.concat([head, body, crc]);
}

function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6; // @note 8-bit RGBA
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
  ]);
}

/* @return the file inside dir, or null for a name that tries to leave it */
function safePath(dir, file) {
  if (!/^[A-Za-z0-9_./-]{1,96}$/.test(file) || file.includes('..') || !file.endsWith('.rttex')) return null;
  const full = path.resolve(dir, file);
  return full.startsWith(path.resolve(dir) + path.sep) ? full : null;
}

function sheet(dir, file) {
  const key = `${dir}|${file}`;
  if (sheets.has(key)) return sheets.get(key);
  let img = null;
  const full = safePath(dir, file);
  try { if (full) img = decode(fs.readFileSync(full)); } catch { img = null; }
  if (sheets.size >= MAX_SHEETS) sheets.delete(sheets.keys().next().value);
  sheets.set(key, img);
  return img;
}

/* @return a PNG of the item's 32x32 cell, or null when the texture isn't there */
function icon(dir, file, cx, cy) {
  const key = `${dir}|${file}:${cx}:${cy}`;
  if (icons.has(key)) return icons.get(key);
  const img = sheet(dir, file);
  let out = null;
  if (img && (cx + 1) * CELL <= img.w && (cy + 1) * CELL <= img.h) {
    const cell = Buffer.alloc(CELL * CELL * 4);
    for (let y = 0; y < CELL; y++) {
      const from = ((cy * CELL + y) * img.w + cx * CELL) * 4;
      img.rgba.copy(cell, y * CELL * 4, from, from + CELL * 4);
    }
    out = png(CELL, CELL, cell);
  }
  if (icons.size >= MAX_ICONS) icons.delete(icons.keys().next().value);
  icons.set(key, out);
  return out;
}

/* @return the whole decoded sheet, or null */
function sheetOf(dir, file) { return sheet(dir, file); }

/* @return the whole sheet as a PNG (the resource manager's preview), or null */
function sheetPng(dir, file) {
  const img = sheet(dir, file);
  return img ? png(img.w, img.h, img.rgba) : null;
}

/* @note after a file is deleted */
function forget(dir, file) {
  sheets.delete(`${dir}|${file}`);
  for (const key of [...icons.keys()]) if (key.startsWith(`${dir}|${file}:`)) icons.delete(key);
}

function clear() { sheets.clear(); icons.clear(); }

module.exports = { decode, png, icon, safePath, sheetOf, sheetPng, forget, clear };
