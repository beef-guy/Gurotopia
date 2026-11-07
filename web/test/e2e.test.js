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
      body: form ? new URLSearchParams(Object.entries(form).flatMap(([k, val]) => [].concat(val).map((x) => [k, x]))).toString() : undefined,
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
    SAMPLE_SECONDS: '1', STORAGE_CHECK_MINUTES: '1',
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

  await t.test('dashboard, pages and resource graphs', async () => {
    const dash = await alice.get('/servers');
    assert.equal(dash.status, 200);
    assert.match(dash.text, /SkyBlock/);
    assert.match(dash.text, /Network load/);
    const live = JSON.parse((await alice.get('/dashboard.json')).text);
    assert.ok(live.platform.online >= 1);
    assert.equal(live.platform.top.name, 'SkyBlock');
    assert.ok(live.host.memory.total > 0);

    for (const page of ['', '/console', '/settings']) {
      const r = await alice.get(`/servers/${id}${page}`);
      assert.equal(r.status, 200, page);
      assert.match(r.text, /class="side-link active"/);
    }

    const metrics = await waitFor(async () => {
      const m = JSON.parse((await alice.get(`/servers/${id}/metrics.json?minutes=15`)).text);
      return m.samples.some((s) => s.mem > 0 && s.cpu !== null && s.players === 2) ? m : null;
    }, 'resource samples', 10000);
    assert.equal(metrics.intervalSeconds, 1);
    assert.ok(metrics.storage && metrics.storage.files > 0, 'storage was measured');
    assert.equal((await bob.get(`/servers/${id}/metrics.json`)).status, 404);
  });

  await t.test('players page lists accounts and moderation changes them', async () => {
    let r = await alice.get(`/servers/${id}/players`);
    assert.equal(r.status, 200);
    assert.match(r.text, /No player data yet/, 'the fake game has no peer table');

    const srv = `srv_${id}`;
    await admin.query(`CREATE TABLE ${srv}.peer (uid INT AUTO_INCREMENT PRIMARY KEY, growid VARCHAR(18) UNIQUE, password VARCHAR(128),
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, inventory MEDIUMBLOB, gems BIGINT DEFAULT 0, wl_storage INT DEFAULT 0, level INT DEFAULT 1,
      xp INT DEFAULT 0, role INT DEFAULT 0, banned_until BIGINT DEFAULT 0, ban_reason VARCHAR(128) DEFAULT '', muted_until BIGINT DEFAULT 0,
      playtime BIGINT DEFAULT 0, piggy_bank BIGINT DEFAULT 0)`);
    await admin.query(`CREATE TABLE ${srv}.mod_log (id INT AUTO_INCREMENT PRIMARY KEY, at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      actor VARCHAR(18), action VARCHAR(16), target VARCHAR(18), detail VARCHAR(255))`);
    await admin.query(`INSERT INTO ${srv}.peer (growid, gems, level, role, password) VALUES ('Psy7', 5000, 40, 2, 'secret-hash'), ('Noob', 10, 2, 0, 'x'), ('Griefer', 0, 1, 0, 'y')`);

    r = await alice.get(`/servers/${id}/players`);
    assert.match(r.text, /Psy7/);
    assert.match(r.text, /Developer/);
    assert.doesNotMatch(r.text, /secret-hash/, 'passwords never reach the page');
    r = await alice.get(`/servers/${id}/players?q=gri`);
    assert.match(r.text, /Griefer/);
    assert.doesNotMatch(r.text, />Noob</);

    const [[g]] = await admin.query(`SELECT uid FROM ${srv}.peer WHERE growid = 'Griefer'`);
    const base = `/servers/${id}/players/${g.uid}`;
    assert.equal((await alice.get(base)).status, 200);
    assert.equal((await bob.get(base)).status, 404, 'other owners cannot see players');
    assert.equal((await bob.post(`${base}/ban`, { _csrf: await bob.csrf(), duration: 'forever' })).status, 404);

    r = await alice.post(`${base}/ban`, { _csrf: await alice.csrf(), duration: 'nonsense' });
    assert.equal(r.status, 400);
    r = await alice.post(`${base}/ban`, { _csrf: await alice.csrf(), duration: 'forever', reason: 'griefing' });
    assert.equal(r.status, 302);
    let [[row]] = await admin.query(`SELECT banned_until, ban_reason FROM ${srv}.peer WHERE uid = ?`, [g.uid]);
    assert.equal(Number(row.banned_until), -1);
    assert.equal(row.ban_reason, 'griefing');
    assert.match((await alice.get(`/servers/${id}/players?filter=banned`)).text, /Griefer/);

    await alice.post(`${base}/mute`, { _csrf: await alice.csrf(), duration: '1h' });
    await alice.post(`${base}/role`, { _csrf: await alice.csrf(), role: '1' });
    await alice.post(`${base}/unban`, { _csrf: await alice.csrf() });
    [[row]] = await admin.query(`SELECT banned_until, muted_until, role FROM ${srv}.peer WHERE uid = ?`, [g.uid]);
    assert.equal(Number(row.banned_until), 0);
    assert.ok(Number(row.muted_until) > Date.now() / 1000);
    assert.equal(row.role, 1);
    assert.equal((await alice.post(`${base}/role`, { _csrf: await alice.csrf(), role: '7' })).status, 400);

    const [log] = await admin.query(`SELECT actor, action FROM ${srv}.mod_log ORDER BY id`);
    assert.deepEqual(log.map((l) => l.action), ['ban', 'mute', 'role', 'unban']);
    const [sent] = await admin.query(`SELECT action, target, done_at FROM ${srv}.panel_actions ORDER BY id`);
    assert.deepEqual(sent.map((a) => `${a.action} ${a.target}`), Array(4).fill('player_sync Griefer'), 'the running game was told each time');
    assert.ok(sent.every((a) => a.done_at), 'and carried each one out');

    r = await alice.get(`/servers/${id}/players`);
    assert.match(r.text, /in TRADE/, 'shows who is online and where');
    r = await alice.post(`${base}/kick`, { _csrf: await alice.csrf(), reason: 'spam' });
    assert.equal(r.status, 302);
    assert.match(JSON.parse((await alice.get(`/servers/${id}/logs.json`)).text).text, /panel action kick Griefer spam/);
    assert.equal(log[0].actor, 'web:alice');
    assert.match((await alice.get(base)).text, /Moderation history[\s\S]*unban/);
  });

  await t.test('worlds page lists worlds and deletes them through the game', async () => {
    const srv = `srv_${id}`;
    await admin.query(`CREATE TABLE ${srv}.world (name VARCHAR(24) NOT NULL PRIMARY KEY, owner INT DEFAULT 0, blocks MEDIUMBLOB,
      objects MEDIUMBLOB, access VARCHAR(512) DEFAULT '', min_level INT DEFAULT 1, extras MEDIUMBLOB)`);
    const [[psy]] = await admin.query(`SELECT uid FROM ${srv}.peer WHERE growid = 'Psy7'`);
    const [[noob]] = await admin.query(`SELECT uid FROM ${srv}.peer WHERE growid = 'Noob'`);
    await admin.query(`INSERT INTO ${srv}.world (name, owner, blocks, access, min_level) VALUES
      ('TRADE', ?, REPEAT('x', 3000), ?, 5), ('START', 0, REPEAT('x', 100), '', 1)`, [psy.uid, String(noob.uid)]);
    await admin.query(`REPLACE INTO ${srv}.panel_live (growid, world) VALUES ('Griefer', 'TRADE')`); // @note the players test kicked Griefer

    let r = await alice.get(`/servers/${id}/worlds`);
    assert.equal(r.status, 200);
    assert.match(r.text, /TRADE/);
    assert.match(r.text, /START/);
    r = await alice.get(`/servers/${id}/worlds?filter=active`);
    assert.match(r.text, /TRADE/);
    assert.doesNotMatch(r.text, />START</);

    r = await alice.get(`/servers/${id}/worlds/TRADE`);
    assert.equal(r.status, 200);
    assert.match(r.text, /Psy7/);
    assert.match(r.text, /Noob/, 'access list names');
    assert.match(r.text, /Griefer/, 'who is inside');
    assert.equal((await bob.get(`/servers/${id}/worlds/TRADE`)).status, 404);
    assert.equal((await alice.get(`/servers/${id}/worlds/NOPE`)).status, 404);

    r = await alice.post(`/servers/${id}/worlds/TRADE/empty`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    r = await alice.post(`/servers/${id}/worlds/TRADE/delete`, { _csrf: await alice.csrf(), confirm: 'nope' });
    assert.equal(r.status, 400);
    r = await alice.post(`/servers/${id}/worlds/TRADE/delete`, { _csrf: await alice.csrf(), confirm: 'trade' });
    assert.equal(r.status, 302);
    assert.equal(r.location, `/servers/${id}/worlds?done=worldDeleted`);
    const [left] = await admin.query(`SELECT name FROM ${srv}.world`);
    assert.deepEqual(left.map((w) => w.name), ['START'], 'the game deleted it');
    const [acts] = await admin.query(`SELECT action FROM ${srv}.panel_actions WHERE action LIKE 'world%' ORDER BY id`);
    assert.deepEqual(acts.map((a) => a.action), ['world_empty', 'world_delete']);
  });

  await t.test('roles can be made, ordered, given and deleted', async () => {
    const srv = `srv_${id}`;
    let r = await alice.get(`/servers/${id}/roles`);
    assert.equal(r.status, 200);
    assert.match(r.text, /Developer[\s\S]*Moderator[\s\S]*Player/, 'highest first');

    r = await alice.get(`/servers/${id}/roles/new`);
    assert.match(r.text, /\/newthing/, 'commands the game has but the website does not describe are listed too');
    r = await alice.post(`/servers/${id}/roles`, { _csrf: await alice.csrf(), name: 'VIP', color: 'e', tag: '+', powers: '0', commands: ['/sb', '/give', '/bogus'] });
    assert.equal(r.status, 302);
    const vipId = Number(r.location.match(/roles\/(\d+)/)[1]);
    assert.equal(vipId, 3);
    let [[vip]] = await admin.query(`SELECT * FROM ${srv}.roles WHERE id = 3`);
    assert.equal(vip.commands, '/give,/sb', 'unknown commands are dropped');
    assert.equal(vip.color, 'e');
    const [[mod]] = await admin.query(`SELECT priority FROM ${srv}.roles WHERE id = 1`);
    assert.ok(vip.priority < mod.priority && vip.priority > 0, 'a new role starts just above Player');

    assert.equal((await alice.post(`/servers/${id}/roles`, { _csrf: await alice.csrf(), name: 'vip', color: '', powers: '0' })).status, 400, 'names are unique');
    assert.equal((await alice.post(`/servers/${id}/roles`, { _csrf: await alice.csrf(), name: 'Bad', color: '', tag: '`4', powers: '0' })).status, 400, 'no colour codes in tags');

    r = await alice.post(`/servers/${id}/roles/3/up`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    [[vip]] = await admin.query(`SELECT priority FROM ${srv}.roles WHERE id = 3`);
    const [[mod2]] = await admin.query(`SELECT priority FROM ${srv}.roles WHERE id = 1`);
    assert.ok(vip.priority > mod2.priority, 'moved above Moderator');
    assert.equal((await alice.post(`/servers/${id}/roles/2/down`, { _csrf: await alice.csrf() })).status, 400, 'Developer stays on top');

    r = await alice.post(`/servers/${id}/roles/2`, { _csrf: await alice.csrf(), name: 'Owner', color: '4', tag: '@', powers: '0', commands: ['/sb'] });
    assert.equal(r.status, 302);
    const [[dev]] = await admin.query(`SELECT name, powers, commands FROM ${srv}.roles WHERE id = 2`);
    assert.equal(dev.name, 'Owner');
    assert.equal(dev.powers, 2, 'the top role keeps every power');
    assert.match(dev.commands, /\/give/, 'and its commands');

    const [[g]] = await admin.query(`SELECT uid FROM ${srv}.peer WHERE growid = 'Griefer'`);
    await alice.post(`/servers/${id}/players/${g.uid}/role`, { _csrf: await alice.csrf(), role: '3' });
    assert.match((await alice.get(`/servers/${id}/players/${g.uid}`)).text, /gt-sky">\+VIP/);

    assert.equal((await alice.post(`/servers/${id}/roles/1/delete`, { _csrf: await alice.csrf() })).status, 400, 'built-in roles stay');
    r = await alice.post(`/servers/${id}/roles/3/delete`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [[after]] = await admin.query(`SELECT role FROM ${srv}.peer WHERE uid = ?`, [g.uid]);
    assert.equal(after.role, 0, 'its players became Players');

    const [sent] = await admin.query(`SELECT done_at FROM ${srv}.panel_actions WHERE action = 'roles_reload'`);
    assert.equal(sent.length, 4, 'the game re-read the roles after each change');
    assert.ok(sent.every((a) => a.done_at));
    const [log] = await admin.query(`SELECT action FROM ${srv}.mod_log WHERE action LIKE 'role%' ORDER BY id`);
    assert.deepEqual(log.map((l) => l.action).slice(-5), ['role_create', 'role_move', 'role_edit', 'role', 'role_delete']);
  });

  await t.test('item manager edits items and makes new ones through the game', async () => {
    const srv = `srv_${id}`;
    let r = await alice.get(`/servers/${id}/items`);
    assert.equal(r.status, 200);
    assert.match(r.text, /Legendary Wings/);
    assert.match((await alice.get(`/servers/${id}/items?q=wings`)).text, /Legendary Wings[\s\S]*2 items</);
    assert.match((await alice.get(`/servers/${id}/items?q=%234`)).text, /World Lock/, 'search by #id');
    assert.match((await alice.get(`/servers/${id}/items?filter=clothing`)).text, /Legendary Wings/);
    assert.doesNotMatch((await alice.get(`/servers/${id}/items?filter=clothing`)).text, />Dirt</);

    r = await alice.get(`/servers/${id}/items/2`);
    assert.equal(r.status, 200);
    assert.match(r.text, /This is Dirt\./);
    r = await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Golden Dirt', info: 'Shiny.', rarity: '42', hits: '9', hitReset: '12',
      collision: '1', untradeable: '1', texture: 'tiles_page2.rttex', texX: '3', texY: '4' });
    assert.equal(r.status, 302);
    let [[edit]] = await admin.query(`SELECT * FROM ${srv}.item_edits WHERE id = 2`);
    assert.equal(edit.name, 'Golden Dirt');
    assert.equal(edit.cat, 0x80, 'untradeable flag set, nothing else');
    assert.equal(edit.rarity, 42);
    const [[cat]] = await admin.query(`SELECT name, rarity FROM ${srv}.item_catalog WHERE id = 2`);
    assert.equal(cat.name, 'Golden Dirt', 'the game rebuilt items.dat and its list');
    assert.match((await alice.get(`/servers/${id}/items?filter=edited`)).text, /Golden Dirt/);
    assert.equal((await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Bad|Name', info: '', rarity: '1', hits: '1', hitReset: '1', collision: '1', texture: 'a.rttex', texX: '0', texY: '0' })).status, 400);
    assert.equal((await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Ok', info: '', rarity: '1', hits: '1', hitReset: '1', collision: '1', texture: '../x.rttex', texX: '0', texY: '0' })).status, 400);

    const found = JSON.parse((await alice.get(`/servers/${id}/items/search.json?q=wings`)).text);
    assert.equal(found.items[0].id, 6);
    r = await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '7', name: 'Emerald Wings' });
    assert.equal(r.status, 302);
    const newId = Number(r.location.match(/items\/(\d+)/)[1]);
    assert.equal(newId, 8, 'first even id after the game\'s items');
    const [made] = await admin.query(`SELECT id, base_id, name FROM ${srv}.item_edits WHERE base_id IS NOT NULL ORDER BY id`);
    assert.deepEqual(made.map((m) => [m.id, m.base_id, m.name]), [[8, 6, 'Emerald Wings'], [9, 7, 'Emerald Wings Seed']], 'a seed picked the block, and the seed came too');
    assert.match((await alice.get(`/servers/${id}/items?filter=custom`)).text, /Emerald Wings/);
    assert.equal((await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '6', name: 'emerald wings' })).status, 400, 'names are unique');
    assert.equal((await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '8', name: 'Copy Of Copy' })).status, 400, 'only from the game\'s own items');

    r = await alice.post(`/servers/${id}/items/2/reset`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    assert.equal((await admin.query(`SELECT * FROM ${srv}.item_edits WHERE id = 2`))[0].length, 0, 'reset removes the edit');
    r = await alice.post(`/servers/${id}/items/8/reset`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [gone] = await admin.query(`SELECT removed FROM ${srv}.item_edits WHERE id IN (8, 9)`);
    assert.ok(gone.every((g) => g.removed === 1), 'deleting a custom item removes its seed too');
    assert.equal((await alice.get(`/servers/${id}/items/8`)).status, 404);

    const [sent] = await admin.query(`SELECT done_at FROM ${srv}.panel_actions WHERE action = 'items_reload'`);
    assert.equal(sent.length, 4);
    assert.ok(sent.every((a) => a.done_at));
    assert.equal((await alice.get(`/servers/${id}/items/2/icon.png`)).status, 404, 'no textures on this machine: the page shows a letter instead');
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
