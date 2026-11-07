'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { LogFile, readTail, gameArgs } = require('../src/supervisor');
const { nameProblem, maxPlayersProblem } = require('../src/servers');
const { describe } = require('../src/panel');

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
