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
