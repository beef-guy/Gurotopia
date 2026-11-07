'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { LogFile, readTail, gameArgs } = require('../src/supervisor');
const { nameProblem, maxPlayersProblem } = require('../src/servers');
const { describe, fmt } = require('../src/panel');

test('server names', () => {
  assert.equal(nameProblem('SkyBlock'), null);
  assert.equal(nameProblem('abc123'), null);
  assert.ok(nameProblem('ab'));
  assert.ok(nameProblem('has space'));
  assert.ok(nameProblem('semi;colon'));
  assert.ok(nameProblem('a'.repeat(25)));
  assert.ok(nameProblem('ADMIN'));
  assert.ok(nameProblem(undefined));
});

test('max players', () => {
  assert.equal(maxPlayersProblem('50'), null);
  assert.ok(maxPlayersProblem('0'));
  assert.ok(maxPlayersProblem('1.5'));
  assert.ok(maxPlayersProblem('101'));
  assert.ok(maxPlayersProblem('abc'));
});

test('game command line', () => {
  assert.deepEqual(gameArgs({ name: 'Sky', port: 17100, db_name: 'srv_1', max_players: 20 }), [
    '--name', 'Sky', '--port', '17100', '--database', 'srv_1',
    '--platform-db', 'gurotopia_platform', '--no-https', '--max-players', '20',
  ]);
});

test('log file joins split lines, rotates, and tails', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gt-log-'));
  const file = path.join(dir, 'logs', 'server.log');
  const log = new LogFile(file, 400);
  log.feed('stdout', Buffer.from('hello wo'));
  log.feed('stdout', Buffer.from('rld\nsecond'));
  log.feed('stderr', Buffer.from('oops\n'));
  log.close();
  const text = fs.readFileSync(file, 'utf8');
  assert.match(text, /\] hello world\n/);
  assert.match(text, /\] \[err\] oops\n/);
  assert.match(text, /\] second\n/, 'close flushes a half line');

  for (let i = 0; i < 20; i++) log.note(`line ${i}`);
  log.close();
  assert.ok(fs.existsSync(`${file}.1`), 'rotated');
  assert.ok(fs.statSync(file).size <= 400);

  const tail = await readTail(file, 60);
  assert.ok(tail.length <= 60);
  assert.match(tail, /line 19\n$/);
  assert.ok(!tail.startsWith('ine'), 'first partial line dropped');
  assert.equal(await readTail(path.join(dir, 'missing.log'), 100), '');
  fs.rmSync(dir, { recursive: true, force: true });
});

test('status labels', () => {
  assert.equal(describe({ reporting: true }, { state: 'running' }).label, 'Online');
  assert.equal(describe({ reporting: false }, { state: 'running' }).label, 'Starting');
  assert.equal(describe({ reporting: false }, { state: 'stopped' }).label, 'Offline');
  assert.equal(describe({ reporting: false }, { state: 'crashed', message: 'x' }).label, 'Crashed');
  assert.equal(describe({ reporting: false }, null).label, 'Unknown');
});

test('login page: token carries the server name, checktoken keeps it', async () => {
  const { createApp } = require('../src/login');
  const server = createApp().listen(0, '127.0.0.1');
  await new Promise((r) => server.once('listening', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (p, fields) => fetch(base + p, {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(fields).toString(),
  }).then((r) => r.text());
  const decode = (t) => Buffer.from(JSON.parse(t).token, 'base64').toString();
  try {
    const login = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'login', server: 'Sky1', growId: 'Bob', password: 'pass12', password_confirmation: '' });
    assert.equal(decode(login), '_token=YWJj&growId=Bob&password=pass12&reg=0&server=Sky1');

    const reg = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'register', server: 'Sky1', growId: 'Bob', password: 'pass12', password_confirmation: 'pass12' });
    assert.match(decode(reg), /&reg=1&server=Sky1$/);

    const mismatch = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'register', server: 'Sky1', growId: 'Bob', password: 'pass12', password_confirmation: 'other1' });
    assert.match(mismatch, /class="error"/);

    const noServer = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'login', server: '', growId: 'Bob', password: 'pass12' });
    assert.match(noServer, /class="error"/);

    const evil = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'login', server: 'Sky1', growId: 'Bob', password: 'x&reg=1' });
    assert.match(evil, /class="error"/); // @note & would let a player inject token fields

    const xss = await post('/player/growid/login/validate', { _token: 'YWJj', mode: 'login', server: '"><script>', growId: 'Bob', password: 'pass12' });
    assert.ok(!xss.includes('<script>alert') && !xss.includes('"><script>'));

    const redirect = await fetch(base + '/player/growid/checktoken', { method: 'POST', redirect: 'manual' });
    assert.equal(redirect.status, 307);
    assert.equal(redirect.headers.get('location'), '/player/growid/validate/checktoken');

    const check = await post('/player/growid/checktoken', { refreshToken: JSON.parse(login).token, clientData: 'xyz' }); // @note fetch follows the 307 with the body
    assert.equal(decode(check), `_token=${Buffer.from('xyz').toString('base64')}&growId=Bob&password=pass12&reg=0&server=Sky1`);
  } finally {
    server.close();
  }
});

test('number formatting for the dashboard', () => {
  assert.equal(fmt.bytes(512), '512 B');
  assert.equal(fmt.bytes(1536), '1.5 KB');
  assert.equal(fmt.bytes(300 * 1024 * 1024), '300 MB');
  assert.equal(fmt.bytes(null), '–');
  assert.equal(fmt.duration(59), '0m');
  assert.equal(fmt.duration(3 * 3600 + 120), '3h 2m');
  assert.equal(fmt.duration(2 * 86400 + 5 * 3600), '2d 5h');
  assert.equal(fmt.percent(7.25), '7.3%');
});

test('host stats read /proc', async () => {
  const hostStats = require('../src/hostStats');
  const s = await hostStats.snapshot();
  assert.ok(s.memory.total > 0 && s.memory.used > 0);
  assert.ok(s.cores > 0);
  assert.ok(s.uptimeSeconds > 0);
});

test('item icons: a 32x32 cell of an .rttex becomes a PNG', () => {
  const rttex = require('../src/rttex');
  const zlib = require('node:zlib');
  const os = require('node:os');
  const w = 64; const h = 32;
  const head = Buffer.alloc(124);
  head.write('RTTXTR', 0, 'latin1');
  head.writeInt32LE(h, 8); head.writeInt32LE(w, 12); head.writeInt32LE(0x1401, 16);
  head[28] = 1; // @note alpha
  const pixels = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = (y * w + x) * 4;
    pixels[i] = x < 32 ? 255 : 0; pixels[i + 1] = x >= 32 ? 255 : 0; pixels[i + 2] = 0; pixels[i + 3] = 255;
  }
  const raw = Buffer.concat([head, pixels]);
  const packed = Buffer.alloc(32);
  packed.write('RTPACK', 0, 'latin1');
  packed.writeUInt32LE(raw.length, 12);
  packed[16] = 1;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tex-'));
  fs.writeFileSync(path.join(dir, 'sheet.rttex'), Buffer.concat([packed, zlib.deflateSync(raw)]));

  const decoded = rttex.decode(fs.readFileSync(path.join(dir, 'sheet.rttex')));
  assert.equal(decoded.w, 64);
  assert.equal(decoded.rgba[0], 255, 'left half red');
  assert.equal(decoded.rgba[32 * 4 + 1], 255, 'right half green');
  const icon = rttex.icon(dir, 'sheet.rttex', 1, 0);
  assert.ok(icon && icon.subarray(1, 4).toString() === 'PNG');
  assert.equal(icon.readUInt32BE(16), 32);
  assert.equal(rttex.icon(dir, 'sheet.rttex', 2, 0), null, 'outside the sheet');
  assert.equal(rttex.icon(dir, 'missing.rttex', 0, 0), null);
  assert.equal(rttex.safePath(dir, '../etc/passwd.rttex'), null);
  assert.equal(rttex.safePath(dir, 'sheet.png'), null);
});

test('resources: PNG to .rttex keeps every pixel and pads to a power of two', () => {
  const rttex = require('../src/rttex');
  const resources = require('../src/resources');
  const w = 40; const h = 70;
  const rgba = Buffer.alloc(w * h * 4);
  for (let i = 0; i < w * h; i++) { rgba[i * 4] = i & 0xff; rgba[i * 4 + 1] = (i >> 4) & 0xff; rgba[i * 4 + 2] = 9; rgba[i * 4 + 3] = i % 3 ? 255 : 0; }
  const decoded = resources.decodePng(rttex.png(w, h, rgba));
  assert.ok(decoded.rgba.equals(rgba));
  const back = rttex.decode(resources.encodeRttex(decoded));
  assert.deepEqual([back.w, back.h], [64, 128]);
  for (let y = 0; y < h; y++) assert.ok(back.rgba.subarray(y * 64 * 4, y * 64 * 4 + w * 4).equals(rgba.subarray(y * w * 4, (y + 1) * w * 4)), `row ${y}`);
  assert.equal(back.rgba[(0 * 64 + 50) * 4 + 3], 0, 'padding is see-through');
  assert.throws(() => resources.decodePng(Buffer.from('nope')), resources.UserError);
  assert.equal(resources.isOwnName({ id: 3 }, 'ps3_sword_0123abcd.rttex'), true);
  assert.equal(resources.isOwnName({ id: 3 }, 'ps4_sword_0123abcd.rttex'), false, 'another server\'s file');
  assert.equal(resources.isOwnName({ id: 3 }, 'ps3_../x_0123abcd.rttex'), false);
});
