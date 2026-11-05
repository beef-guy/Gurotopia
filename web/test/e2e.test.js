'use strict';
/*
 * End to end: runs the real panel and supervisor against a local MariaDB, with
 * test/fake-game.js standing in for main.out (or the real one, with GAME_DIR set).
 *
 *   TEST_DB_USER=gurotopia TEST_DB_PASSWORD=... npm test
 *
 * It uses its own platform database (gurotopia_panel_test) and drops it afterwards.
 */
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const mysql = require('mysql2/promise');

const DB_USER = process.env.TEST_DB_USER;
const DB_PASSWORD = process.env.TEST_DB_PASSWORD || '';
const PLATFORM_DB = 'gurotopia_panel_test';
const PANEL_PORT = 3900;
const SUP_PORT = 3901;
const BASE = `http://127.0.0.1:${PANEL_PORT}`;

const skip = DB_USER ? false : 'set TEST_DB_USER (and TEST_DB_PASSWORD) to run against a local MariaDB';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitFor(fn, what, ms = 15000) {
  const end = Date.now() + ms;
  let last;
  while (Date.now() < end) {
    last = await fn();
    if (last) return last;
    await sleep(200);
  }
  throw new Error(`timed out waiting for ${what}`);
}

class Browser {
  constructor() { this.cookie = ''; }
  async req(method, url, form) {
    const res = await fetch(BASE + url, {
      method,
      redirect: 'manual',
      headers: {
        cookie: this.cookie,
        ...(form ? { 'content-type': 'application/x-www-form-urlencoded' } : {}),
      },
      body: form ? new URLSearchParams(form).toString() : undefined,
    });
    const set = res.headers.get('set-cookie');
    if (set) this.cookie = set.split(';')[0];
    const text = await res.text();
    return { status: res.status, location: res.headers.get('location'), text };
  }
  get(url) { return this.req('GET', url); }
  post(url, form) { return this.req('POST', url, form); }
  async csrf(url = '/servers') {
    const { text } = await this.get(url);
    const m = text.match(/name="_csrf" value="([^"]+)"/);
    assert.ok(m, 'page has a csrf token');
    return m[1];
  }
}

test('panel and supervisor', { skip, timeout: 120000 }, async (t) => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gt-panel-'));
  const gameDir = process.env.TEST_GAME_DIR || path.join(tmp, 'game');
  const serversDir = path.join(tmp, 'servers');
  if (!process.env.TEST_GAME_DIR) {
    fs.mkdirSync(path.join(gameDir, 'resources'), { recursive: true });
    fs.writeFileSync(path.join(gameDir, 'items.dat'), 'fake');
    fs.copyFileSync(path.join(__dirname, 'fake-game.js'), path.join(gameDir, 'main.out'));
    fs.chmodSync(path.join(gameDir, 'main.out'), 0o755);
    fs.symlinkSync(path.join(__dirname, '..', 'node_modules'), path.join(gameDir, 'node_modules')); // @note so the fake finds mysql2
    fs.writeFileSync(path.join(gameDir, 'mysql_login.txt'), `host|127.0.0.1\nuser|${DB_USER}\npassword|${DB_PASSWORD}`);
  }

  const admin = await mysql.createConnection({ host: '127.0.0.1', user: DB_USER, password: DB_PASSWORD });
  await admin.query(`DROP DATABASE IF EXISTS ${PLATFORM_DB}`);

  const env = {
    ...process.env,
    DB_HOST: '127.0.0.1', DB_USER, DB_PASSWORD, PLATFORM_DB,
    PANEL_PORT: String(PANEL_PORT), SUPERVISOR_PORT: String(SUP_PORT),
    SUPERVISOR_TOKEN: 'x'.repeat(40),
    GAME_DIR: gameDir, SERVERS_DIR: serversDir,
    STOP_TIMEOUT_SECONDS: '10', MAX_CRASHES: '2',
    SERVERS_PER_USER: '2', SIGNUP_CODE: 'letmein',
  };
  const procs = [];
  const output = [];
  const run = (file) => {
    const p = spawn(process.execPath, [path.join(__dirname, '..', 'src', file)], { env, stdio: ['ignore', 'pipe', 'pipe'] });
    p.stdout.on('data', (d) => output.push(`[${file}] ${d}`));
    p.stderr.on('data', (d) => output.push(`[${file}] ${d}`));
    procs.push(p);
    return p;
  };

  t.after(async () => {
    for (const p of procs) p.kill('SIGTERM');
    await sleep(1500);
    await admin.query(`DROP DATABASE IF EXISTS ${PLATFORM_DB}`);
    const [dbs] = await admin.query("SHOW DATABASES LIKE 'srv\\_%'");
    for (const row of dbs) await admin.query(`DROP DATABASE \`${Object.values(row)[0]}\``);
    await admin.end();
    fs.rmSync(tmp, { recursive: true, force: true });
    if (process.env.TEST_VERBOSE) console.log(output.join(''));
  });

  run('supervisor.js');
  run('panel.js');
  await waitFor(() => fetch(`${BASE}/`).then((r) => r.ok, () => false), 'panel to listen');
  await waitFor(() => fetch(`http://127.0.0.1:${SUP_PORT}/health`).then((r) => r.status === 401, () => false), 'supervisor to listen');

  const alice = new Browser();
  const bob = new Browser();

  await t.test('sign up needs the invite code and a good password', async () => {
    let r = await alice.post('/signup', { username: 'alice', password: 'longpassword', password2: 'longpassword', code: 'nope' });
    assert.equal(r.status, 400);
    assert.match(r.text, /invite code/);
    r = await alice.post('/signup', { username: 'alice', password: 'short', password2: 'short', code: 'letmein' });
    assert.equal(r.status, 400);
    r = await alice.post('/signup', { username: 'alice', password: 'longpassword', password2: 'longpassword', code: 'letmein' });
    assert.equal(r.status, 302);
    assert.equal(r.location, '/servers');
    r = await bob.post('/signup', { username: 'alice', password: 'longpassword', password2: 'longpassword', code: 'letmein' });
    assert.match(r.text, /taken/);
    r = await bob.post('/signup', { username: 'bob', password: 'longpassword', password2: 'longpassword', code: 'letmein' });
    assert.equal(r.status, 302);
  });

  await t.test('signed-out pages redirect to login, wrong password is refused', async () => {
    const anon = new Browser();
    const r = await anon.get('/servers');
    assert.equal(r.status, 302);
    assert.match(r.location, /^\/login/);
    const bad = await anon.post('/login', { username: 'alice', password: 'wrongwrong' });
    assert.equal(bad.status, 400);
    assert.match(bad.text, /Wrong username or password/);
    const good = await anon.post('/login', { username: 'alice', password: 'longpassword' });
    assert.equal(good.status, 302);
  });

  await t.test('a form without the csrf token is refused', async () => {
    const r = await alice.post('/servers', { name: 'NoToken', maxPlayers: '10' });
    assert.equal(r.status, 403);
  });

  let id;
  await t.test('create a server: it gets a port, a database and starts', async () => {
    let r = await alice.post('/servers', { _csrf: await alice.csrf(), name: 'bad name!', maxPlayers: '10' });
    assert.equal(r.status, 400);
    r = await alice.post('/servers', { _csrf: await alice.csrf(), name: 'admin', maxPlayers: '10' });
    assert.match(r.text, /reserved/);
    r = await alice.post('/servers', { _csrf: await alice.csrf(), name: 'SkyBlock', maxPlayers: '20' });
    assert.equal(r.status, 302, r.text);
    id = Number(r.location.match(/\/servers\/(\d+)/)[1]);

    const [[row]] = await admin.query(`SELECT * FROM ${PLATFORM_DB}.servers WHERE id = ?`, [id]);
    assert.equal(row.name, 'SkyBlock');
    assert.equal(row.port, 17100);
    assert.equal(row.db_name, `srv_${id}`);
    assert.equal(row.max_players, 20);

    r = await bob.post('/servers', { _csrf: await bob.csrf(), name: 'skyblock', maxPlayers: '20' });
    assert.match(r.text, /taken/, 'names are case-insensitive');

    await waitFor(async () => (await alice.get(`/servers/${id}/status.json`)).text.includes('"Online"'), 'Online');
    const status = JSON.parse((await alice.get(`/servers/${id}/status.json`)).text);
    assert.equal(status.online, 2);
    assert.ok(fs.lstatSync(path.join(serversDir, String(id), 'items.dat')).isSymbolicLink());
    const [dbs] = await admin.query(`SHOW DATABASES LIKE 'srv\\_${id}'`);
    assert.equal(dbs.length, 1, 'first start created the server database');
  });

  await t.test('the log shows the server output', async () => {
    const log = await waitFor(async () => {
      const { text } = JSON.parse((await alice.get(`/servers/${id}/logs.json`)).text);
      return text.includes('ready') ? text : null;
    }, 'log');
    assert.match(log, /fake server SkyBlock on port 17100, database srv_\d+, max 20/);
    assert.match(log, /\[supervisor\] starting SkyBlock on port 17100/);
  });

  await t.test('other owners cannot see or control it', async () => {
    assert.equal((await bob.get(`/servers/${id}`)).status, 404);
    assert.equal((await bob.get(`/servers/${id}/logs.json`)).status, 404);
    const r = await bob.post(`/servers/${id}/stop`, { _csrf: await bob.csrf() });
    assert.equal(r.status, 404);
  });

  await t.test('stop saves and marks it stopped, start brings it back', async () => {
    let r = await alice.post(`/servers/${id}/stop`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [[row]] = await admin.query(`SELECT status, online FROM ${PLATFORM_DB}.servers WHERE id = ?`, [id]);
    assert.equal(row.status, 'stopped');
    const { text } = JSON.parse((await alice.get(`/servers/${id}/logs.json`)).text);
    assert.match(text, /saving everything/);
    assert.match(text, /exited \(code 0\)/);
    assert.match((await alice.get(`/servers/${id}/status.json`)).text, /"Offline"/);

    r = await alice.post(`/servers/${id}/start`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    await waitFor(async () => (await alice.get(`/servers/${id}/status.json`)).text.includes('"Online"'), 'Online again');
  });

  await t.test('settings change max players', async () => {
    let r = await alice.post(`/servers/${id}/settings`, { _csrf: await alice.csrf(), maxPlayers: '5000' });
    assert.equal(r.status, 400);
    r = await alice.post(`/servers/${id}/settings`, { _csrf: await alice.csrf(), maxPlayers: '30' });
    assert.equal(r.status, 302);
    r = await alice.post(`/servers/${id}/restart`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    await waitFor(async () => JSON.parse((await alice.get(`/servers/${id}/logs.json`)).text).text.includes('max 30'), 'restart with new max');
  });

  await t.test('a crashing server is retried, then given up on', async () => {
    const r = await alice.post('/servers', { _csrf: await alice.csrf(), name: 'CrashTest', maxPlayers: '10' });
    assert.equal(r.status, 302);
    const crashId = Number(r.location.match(/\/servers\/(\d+)/)[1]);
    await waitFor(async () => (await alice.get(`/servers/${crashId}/status.json`)).text.includes('"Crashed"'), 'Crashed', 20000);
    const { text } = JSON.parse((await alice.get(`/servers/${crashId}/logs.json`)).text);
    assert.match(text, /restarting in 2 s/);
    assert.match(text, /crashed 2 times/);

    const limit = await alice.post('/servers', { _csrf: await alice.csrf(), name: 'Third', maxPlayers: '10' });
    assert.match(limit.text, /up to 2 servers/);

    const del = await alice.post(`/servers/${crashId}/delete`, { _csrf: await alice.csrf(), confirm: 'CrashTest' });
    assert.equal(del.status, 302);
  });

  await t.test('servers that were running come back after the supervisor restarts', async () => {
    const sup = procs[0];
    sup.kill('SIGTERM');
    await new Promise((r) => sup.once('exit', r));
    const [[row]] = await admin.query(`SELECT status FROM ${PLATFORM_DB}.servers WHERE id = ?`, [id]);
    assert.equal(row.status, 'stopped', 'shutdown stopped it cleanly');
    assert.match((await alice.get(`/servers/${id}/status.json`)).text, /"Unknown"/);
    run('supervisor.js');
    await waitFor(async () => (await alice.get(`/servers/${id}/status.json`)).text.includes('"Online"'), 'Online after supervisor restart');
  });

  await t.test('delete needs the name typed, then removes everything', async () => {
    let r = await alice.post(`/servers/${id}/delete`, { _csrf: await alice.csrf(), confirm: 'wrong' });
    assert.equal(r.status, 400);
    r = await alice.post(`/servers/${id}/delete`, { _csrf: await alice.csrf(), confirm: 'SkyBlock' });
    assert.equal(r.status, 302);
    assert.equal(r.location, '/servers?done=deleted');
    const [rows] = await admin.query(`SELECT id FROM ${PLATFORM_DB}.servers WHERE id = ?`, [id]);
    assert.equal(rows.length, 0);
    const [dbs] = await admin.query(`SHOW DATABASES LIKE 'srv\\_${id}'`);
    assert.equal(dbs.length, 0);
    assert.equal(fs.existsSync(path.join(serversDir, String(id))), false);
    assert.equal(fs.existsSync(path.join(gameDir, 'items.dat')), true, 'shared files were not touched');
  });

  await t.test('log out ends the session', async () => {
    const r = await alice.post('/logout', { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    assert.equal((await alice.get('/servers')).status, 302);
  });
});
