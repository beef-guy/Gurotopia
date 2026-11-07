'use strict';
/*
 * Resource manager: a server's own textures for custom items.
 *
 * An owner uploads a PNG or an .rttex. A PNG is turned into an .rttex here. Each file is kept in
 * CDN_DIR/<server id>/cache/game/, which nginx serves at https://<CDN_HOST>/cdn/<server id>/cache/.
 * The game server tells the client to download missing files from there (--cdn-host/--cdn-path),
 * so players get the textures the first time they see an item that uses one.
 *
 * File names carry the server id and a hash of the picture (ps1_sword_3fa2c1d0.rttex). The client
 * keeps downloaded files by name, so a changed picture always gets a new name and nobody sees an old copy.
 */
const crypto = require('node:crypto');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const zlib = require('node:zlib');
const rttex = require('./rttex');

const MAX_SIDE = 1024; // @note Growtopia's own sheets are at most 1024x1024
const MAX_UPLOAD = 2 * 1024 * 1024;
const MAX_FILES = 100;
const MAX_TOTAL = 40 * 1024 * 1024;

class UserError extends Error {}

/* ---------- PNG -> RGBA ---------- */

function paeth(a, b, c) {
  const p = a + b - c;
  const pa = Math.abs(p - a); const pb = Math.abs(p - b); const pc = Math.abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* @return { w, h, rgba } for a non-interlaced PNG with 8-bit channels (or 1/2/4-bit grey/palette) */
function decodePng(buf) {
  const SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (buf.length < 33 || !buf.subarray(0, 8).equals(SIG)) throw new UserError('That file is not a PNG.');
  let pos = 8;
  let ihdr = null; let palette = null; let trns = null;
  const idat = [];
  while (pos + 8 <= buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('latin1', pos + 4, pos + 8);
    if (pos + 12 + len > buf.length) throw new UserError('That PNG is cut off.');
    const body = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') ihdr = body;
    else if (type === 'PLTE') palette = body;
    else if (type === 'tRNS') trns = body;
    else if (type === 'IDAT') idat.push(body);
    else if (type === 'IEND') break;
    pos += 12 + len;
  }
  if (!ihdr || ihdr.length < 13 || idat.length === 0) throw new UserError('That PNG is missing its picture data.');
  const w = ihdr.readUInt32BE(0); const h = ihdr.readUInt32BE(4);
  const depth = ihdr[8]; const color = ihdr[9]; const interlace = ihdr[12];
  if (w === 0 || h === 0 || w > MAX_SIDE || h > MAX_SIDE) throw new UserError(`The picture must be at most ${MAX_SIDE}×${MAX_SIDE} pixels (this one is ${w}×${h}).`);
  if (interlace !== 0) throw new UserError('Interlaced PNGs aren\'t supported. Save it again without interlacing.');
  const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[color];
  if (!channels || (depth !== 8 && !((color === 0 || color === 3) && [1, 2, 4].includes(depth)))) {
    throw new UserError('Save the PNG as 8-bit RGBA (16-bit colour isn\'t supported).');
  }
  if (color === 3 && !palette) throw new UserError('That PNG has no colour palette.');
  let raw;
  try { raw = zlib.inflateSync(Buffer.concat(idat), { maxOutputLength: (w * 4 + 1) * h + 1024 }); } catch { throw new UserError('That PNG is damaged.'); }
  const bpp = Math.max(1, (channels * depth) >> 3); // @note bytes per pixel for the filters
  const stride = Math.ceil((w * channels * depth) / 8);
  if (raw.length < (stride + 1) * h) throw new UserError('That PNG is damaged.');
  const lines = Buffer.alloc(stride * h);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)];
    const src = y * (stride + 1) + 1;
    const out = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? lines[out + x - bpp] : 0;
      const b = y > 0 ? lines[out - stride + x] : 0;
      const c = x >= bpp && y > 0 ? lines[out - stride + x - bpp] : 0;
      const v = raw[src + x];
      lines[out + x] = (f === 0 ? v : f === 1 ? v + a : f === 2 ? v + b : f === 3 ? v + ((a + b) >> 1) : f === 4 ? v + paeth(a, b, c) : v) & 0xff;
    }
  }
  const rgba = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const d = (y * w + x) * 4;
      const row = y * stride;
      if (depth < 8) {
        const bit = x * depth;
        const v = (lines[row + (bit >> 3)] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
        if (color === 3) {
          rgba[d] = palette[v * 3] ?? 0; rgba[d + 1] = palette[v * 3 + 1] ?? 0; rgba[d + 2] = palette[v * 3 + 2] ?? 0;
          rgba[d + 3] = trns && v < trns.length ? trns[v] : 255;
        } else {
          const g = Math.round((v * 255) / ((1 << depth) - 1));
          rgba[d] = rgba[d + 1] = rgba[d + 2] = g; rgba[d + 3] = 255;
        }
        continue;
      }
      const s = row + x * channels;
      if (color === 6) { rgba[d] = lines[s]; rgba[d + 1] = lines[s + 1]; rgba[d + 2] = lines[s + 2]; rgba[d + 3] = lines[s + 3]; }
      else if (color === 2) { rgba[d] = lines[s]; rgba[d + 1] = lines[s + 1]; rgba[d + 2] = lines[s + 2]; rgba[d + 3] = 255; }
      else if (color === 4) { rgba[d] = rgba[d + 1] = rgba[d + 2] = lines[s]; rgba[d + 3] = lines[s + 1]; }
      else if (color === 0) { rgba[d] = rgba[d + 1] = rgba[d + 2] = lines[s]; rgba[d + 3] = 255; }
      else {
        const v = lines[s];
        rgba[d] = palette[v * 3] ?? 0; rgba[d + 1] = palette[v * 3 + 1] ?? 0; rgba[d + 2] = palette[v * 3 + 2] ?? 0;
        rgba[d + 3] = trns && v < trns.length ? trns[v] : 255;
      }
    }
  }
  return { w, h, rgba };
}

/* ---------- RGBA -> .rttex ---------- */

const pow2 = (n) => { let p = 1; while (p < n) p <<= 1; return p; };

/*
 * @return an RTPACK-wrapped RTTXTR, the way Growtopia ships its textures.
 * @note the game wants power-of-two sizes, so the picture is padded with see-through pixels on the
 *       right and bottom. Cells keep their place, counted from the top left.
 */
function encodeRttex({ w, h, rgba }) {
  const W = pow2(w); const H = pow2(h);
  const header = Buffer.alloc(124);
  header.write('RTTXTR', 0, 'latin1');
  header.writeInt32LE(H, 8); header.writeInt32LE(W, 12);
  header.writeInt32LE(0x1401, 16); // @note GL_UNSIGNED_BYTE
  header.writeInt32LE(H, 20); header.writeInt32LE(W, 24);
  header[28] = 1; // @note has alpha
  header[29] = 0; // @note not compressed
  header.writeInt32LE(1, 32); // @note one mip level
  header.writeInt32LE(H, 100); header.writeInt32LE(W, 104);
  header.writeInt32LE(W * H * 4, 108); header.writeInt32LE(0, 112);
  const pixels = Buffer.alloc(W * H * 4);
  for (let y = 0; y < h; y++) { // @note the file starts at the bottom row
    rgba.copy(pixels, (H - 1 - y) * W * 4, y * w * 4, (y + 1) * w * 4);
  }
  const body = Buffer.concat([header, pixels]);
  const packed = zlib.deflateSync(body, { level: 9 });
  const pack = Buffer.alloc(32);
  pack.write('RTPACK', 0, 'latin1');
  pack[6] = 0;
  pack.writeUInt32LE(packed.length, 8);
  pack.writeUInt32LE(body.length, 12);
  pack[16] = 1; // @note zlib
  return Buffer.concat([pack, packed]);
}

/* ---------- files ---------- */

function dirFor(cdnDir, server) { return path.join(cdnDir, String(server.id), 'cache', 'game'); }

const FILE_RE = /^ps[0-9]{1,9}_[a-z0-9_]{1,40}_[0-9a-f]{8}\.rttex$/;
function isOwnName(server, file) { return FILE_RE.test(file) && file.startsWith(`ps${server.id}_`); }

function slug(name) {
  const s = String(name || '').toLowerCase().replace(/\.(png|rttex)$/, '').replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40);
  return s || 'texture';
}

/* @return the server's textures, newest first: { file, bytes, w, h, cols, rows, added } */
async function list(cdnDir, server) {
  let names = [];
  try { names = await fsp.readdir(dirFor(cdnDir, server)); } catch (err) { if (err.code !== 'ENOENT') throw err; }
  const out = [];
  for (const file of names.filter((n) => isOwnName(server, n))) {
    const stat = await fsp.stat(path.join(dirFor(cdnDir, server), file));
    const img = rttex.sheetOf(dirFor(cdnDir, server), file);
    out.push({
      file, bytes: stat.size, added: stat.mtime,
      w: img ? img.w : 0, h: img ? img.h : 0,
      cols: img ? Math.floor(img.w / 32) : 0, rows: img ? Math.floor(img.h / 32) : 0,
    });
  }
  return out.sort((a, b) => b.added - a.added);
}

/*
 * @param data the uploaded file (PNG or .rttex)
 * @return { file, existed }
 */
async function upload(cdnDir, server, originalName, data) {
  if (!Buffer.isBuffer(data) || data.length === 0) throw new UserError('Pick a PNG or .rttex file first.');
  if (data.length > MAX_UPLOAD) throw new UserError('That file is bigger than 2 MB.');
  let out;
  if (data.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))) {
    out = encodeRttex(decodePng(data));
  } else {
    let img = null;
    try { img = rttex.decode(data); } catch { img = null; }
    if (!img) throw new UserError('That file isn\'t a PNG or an .rttex the game can read (it needs 8-bit RGB or RGBA pixels).');
    if (img.w > MAX_SIDE || img.h > MAX_SIDE) throw new UserError(`The picture must be at most ${MAX_SIDE}×${MAX_SIDE} pixels.`);
    out = encodeRttex(img); // @note stored the same way as a converted PNG, so every file is checked and padded
  }
  const hash = crypto.createHash('sha256').update(out).digest('hex').slice(0, 8);
  const file = `ps${server.id}_${slug(originalName)}_${hash}.rttex`;
  const dir = dirFor(cdnDir, server);
  await fsp.mkdir(dir, { recursive: true, mode: 0o755 });
  const existing = await list(cdnDir, server);
  const same = existing.find((r) => r.file.endsWith(`_${hash}.rttex`));
  if (same) return { file: same.file, existed: true };
  if (existing.length >= MAX_FILES) throw new UserError(`A server can keep ${MAX_FILES} textures. Delete one you don't use first.`);
  if (existing.reduce((n, r) => n + r.bytes, 0) + out.length > MAX_TOTAL) throw new UserError('This server\'s textures would go over 40 MB. Delete one you don\'t use first.');
  const tmp = path.join(dir, `.${file}.${process.pid}.tmp`);
  await fsp.writeFile(tmp, out, { mode: 0o644 });
  await fsp.rename(tmp, path.join(dir, file));
  return { file, existed: false };
}

async function remove(cdnDir, server, file) {
  if (!isOwnName(server, file)) throw new UserError('That texture does not exist.');
  try { await fsp.unlink(path.join(dirFor(cdnDir, server), file)); } catch (err) { if (err.code !== 'ENOENT') throw err; }
  rttex.forget(dirFor(cdnDir, server), file);
}

/* @return the folder a texture name lives in: the server's own uploads, or Growtopia's game folder */
function folderFor(cdnDir, texturesDir, server, file) {
  return isOwnName(server, String(file || '')) ? dirFor(cdnDir, server) : texturesDir;
}

function exists(cdnDir, server, file) {
  return isOwnName(server, file) && fs.existsSync(path.join(dirFor(cdnDir, server), file));
}

module.exports = {
  UserError, MAX_FILES, MAX_UPLOAD, decodePng, encodeRttex, list, upload, remove, folderFor, exists, isOwnName, dirFor,
};
