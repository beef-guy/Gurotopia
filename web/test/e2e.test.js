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
    CDN_DIR: path.join(tmp, 'cdn'), CDN_HOST: 'cdn.test',
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

  await t.test('item effects give items buffs and tell the game', async () => {
    const srv = `srv_${id}`;
    let r = await alice.get(`/servers/${id}/effects`);
    assert.equal(r.status, 200);
    assert.match(r.text, /No items have effects yet/);
    r = await alice.get(`/servers/${id}/effects/new`);
    assert.equal(r.status, 200);
    assert.match(r.text, /One-hit breaking/);

    r = await alice.post(`/servers/${id}/effects`, { _csrf: await alice.csrf(), item: '6', gems: '450', xp: '', oneHit: '1', punchRange: '2', buildRange: '0', drops: '50', harvest: '0',
      bonusItem: '4', bonusChance: '2.5', bonusAmount: '3', note: 'Prize', enabled: '1' });
    assert.equal(r.status, 302, r.text.slice(0, 400));
    assert.match(r.location, /effects\/6\?done=effectCreated/);
    let [[row]] = await admin.query(`SELECT * FROM ${srv}.item_effects WHERE item = 6`);
    assert.deepEqual([row.gems, row.xp, row.one_hit, row.punch_range, row.drops, row.enabled, row.note], [450, 0, 1, 2, 50, 1, 'Prize']);
    assert.deepEqual([row.bonus_item, row.bonus_chance, row.bonus_amount], [4, 250, 3], 'bonus chance is kept in hundredths of a percent');

    r = await alice.get(`/servers/${id}/effects`);
    assert.match(r.text, /Legendary Wings[\s\S]*\+450%[\s\S]*Yes[\s\S]*\+2[\s\S]*2\.5%<\/strong> ×3/, 'the list shows the effects and the bonus drop');
    r = await alice.get(`/servers/${id}/effects?q=dirt`);
    assert.match(r.text, /No item effects match/);
    r = await alice.get(`/servers/${id}/effects?q=6`);
    assert.match(r.text, /Legendary Wings/);

    const bad = async (form, why) => assert.equal((await alice.post(`/servers/${id}/effects`, { _csrf: await alice.csrf(), enabled: '1', ...form })).status, 400, why);
    await bad({ item: '6', gems: '10' }, 'one row per item');
    await bad({ item: '3', gems: '10' }, 'seeds can\'t be picked');
    await bad({ item: '999', gems: '10' }, 'the item must exist');
    await bad({ item: '2', gems: '0' }, 'at least one effect');
    await bad({ item: '2', gems: '-5' }, 'no negative numbers');
    await bad({ item: '2', punchRange: '11' }, 'reach is capped');
    await bad({ item: '2', gems: '1.5' }, 'whole numbers');
    await bad({ item: '2', gems: '5', note: 'a|b' }, 'no | in notes');
    await bad({ item: '2', bonusItem: '999', bonusChance: '5' }, 'the bonus item must exist');
    await bad({ item: '2', bonusItem: '4', bonusChance: '0' }, 'a bonus drop needs a chance');
    await bad({ item: '2', bonusItem: '4', bonusChance: '101' }, 'chance is at most 100');
    await bad({ item: '2', bonusItem: '4', bonusChance: '0.001' }, 'two decimals at most');
    await bad({ item: '2', bonusItem: '4', bonusChance: '5', bonusAmount: '201' }, 'amount is at most 200');
    r = await alice.post(`/servers/${id}/effects`, { _csrf: await alice.csrf(), item: '2', bonusItem: '4', bonusChance: '0.25', enabled: '1' });
    assert.equal(r.status, 302, 'a bonus drop alone is an effect');
    await admin.query(`DELETE FROM ${srv}.item_effects WHERE item = 2`);

    r = await alice.post(`/servers/${id}/effects/6`, { _csrf: await alice.csrf(), gems: '200', xp: '100', punchRange: '0', buildRange: '3', drops: '0', harvest: '25', note: '' });
    assert.equal(r.status, 302);
    [[row]] = await admin.query(`SELECT * FROM ${srv}.item_effects WHERE item = 6`);
    assert.deepEqual([row.gems, row.xp, row.one_hit, row.build_range, row.harvest, row.enabled], [200, 100, 0, 3, 25, 0], 'unticked boxes switch off');
    assert.equal(row.bonus_item, 0, 'an empty bonus item removes the bonus drop');
    r = await alice.get(`/servers/${id}/effects/6`);
    assert.match(r.text, /Save effects/);
    assert.equal((await alice.get(`/servers/${id}/effects/7`)).status, 404);

    r = await alice.post(`/servers/${id}/effects/6/delete`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [gone] = await admin.query(`SELECT item FROM ${srv}.item_effects`);
    assert.equal(gone.length, 0);

    const [sent] = await admin.query(`SELECT action, done_at FROM ${srv}.panel_actions WHERE action = 'effects_reload' ORDER BY id`);
    assert.equal(sent.length, 4);
    assert.ok(sent.every((a) => a.done_at), 'the game carried them out');
  });

  await t.test('title manager makes titles with icons, and players can be given them', async () => {
    const srv = `srv_${id}`;
    let r = await alice.get(`/servers/${id}/titles`);
    assert.equal(r.status, 200);
    assert.match(r.text, /Doctor[\s\S]*Legendary/, 'the real game\'s titles are listed');
    r = await alice.get(`/servers/${id}/titles/new`);
    assert.equal(r.status, 200);

    r = await alice.post(`/servers/${id}/titles`, { _csrf: await alice.csrf(), name: 'Champion', description: 'Won a contest.', prefix: '', suffix: ' the Champ',
      color: '6', badge: 'master', icon: 'item', iconItem: '6', grant: 'manual', sort: '50', enabled: '1' });
    assert.equal(r.status, 302, r.text.slice(0, 400));
    const tid = Number(r.location.match(/titles\/(\d+)/)[1]);
    assert.equal(tid, 100, 'new titles start at 100');
    let [[row]] = await admin.query(`SELECT * FROM ${srv}.titles WHERE id = ?`, [tid]);
    assert.equal(row.suffix, ' the Champ');
    assert.equal(row.texture, 'game/tiles_page1.rttex', 'the icon is the item\'s picture');
    assert.equal(row.tex_x, 6);
    assert.equal(row.badge, 'master');

    assert.equal((await alice.post(`/servers/${id}/titles`, { _csrf: await alice.csrf(), name: 'champion', color: '', badge: '', icon: 'none', grant: 'manual' })).status, 400, 'names are unique');
    assert.equal((await alice.post(`/servers/${id}/titles`, { _csrf: await alice.csrf(), name: 'Pipe', prefix: 'a|b', color: '', badge: '', icon: 'none', grant: 'manual' })).status, 400, 'no | in names');
    assert.equal((await alice.post(`/servers/${id}/titles`, { _csrf: await alice.csrf(), name: 'Tex', color: '', badge: '', icon: 'texture', texture: '../x.rttex', texX: '0', texY: '0', grant: 'manual' })).status, 400, 'texture names are checked');

    r = await alice.post(`/servers/${id}/titles/${tid}`, { _csrf: await alice.csrf(), name: 'Champion', color: '6', badge: '', icon: 'texture', texture: 'tiles_page16.rttex', texX: '27', texY: '11', grant: 'level', grantLevel: '30', sort: '50', enabled: '1' });
    assert.equal(r.status, 302);
    [[row]] = await admin.query(`SELECT * FROM ${srv}.titles WHERE id = ?`, [tid]);
    assert.equal(row.texture, 'game/tiles_page16.rttex');
    assert.equal(row.grant_type, 'level');
    assert.equal(row.grant_value, 30);

    const [[g]] = await admin.query(`SELECT uid FROM ${srv}.peer WHERE growid = 'Griefer'`);
    r = await alice.get(`/servers/${id}/players/${g.uid}`);
    assert.match(r.text, /id="titles"/);
    r = await alice.post(`/servers/${id}/players/${g.uid}/titles`, { _csrf: await alice.csrf(), titles: ['1', '2'] });
    assert.equal(r.status, 302);
    let [given] = await admin.query(`SELECT title, granted, equipped FROM ${srv}.peer_titles WHERE uid = ? ORDER BY title`, [g.uid]);
    assert.deepEqual(given.map((x) => [x.title, x.granted, x.equipped]), [[1, 1, 1], [2, 1, 1]]);
    await alice.post(`/servers/${id}/players/${g.uid}/titles`, { _csrf: await alice.csrf(), titles: ['2'] });
    [given] = await admin.query(`SELECT title, granted FROM ${srv}.peer_titles WHERE uid = ? ORDER BY title`, [g.uid]);
    assert.deepEqual(given.map((x) => [x.title, x.granted]), [[1, 0], [2, 1]], 'Doctor was taken away');

    assert.equal((await alice.post(`/servers/${id}/titles/1/delete`, { _csrf: await alice.csrf() })).status, 400, 'real game titles stay');
    r = await alice.post(`/servers/${id}/titles/${tid}/delete`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [gone] = await admin.query(`SELECT id FROM ${srv}.titles WHERE id = ?`, [tid]);
    assert.equal(gone.length, 0);

    const [sent] = await admin.query(`SELECT action, done_at FROM ${srv}.panel_actions WHERE action LIKE 'titles%' ORDER BY id`);
    assert.deepEqual(sent.map((a) => a.action), ['titles_reload', 'titles_reload', 'titles_sync', 'titles_sync', 'titles_reload']);
    assert.ok(sent.every((a) => a.done_at), 'the game carried them out');
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
    assert.equal((await admin.query(`SELECT name FROM ${srv}.item_catalog WHERE id = 2`))[0][0].name, 'Dirt', 'not in the game until it restarts');
    assert.match((await alice.get(`/servers/${id}/items?filter=edited`)).text, /Golden Dirt/);
    r = await alice.get(`/servers/${id}/items`);
    assert.match(r.text, /1 item change is saved but not in the game yet/);
    assert.match(r.text, /Restart to apply/);
    r = await alice.post(`/servers/${id}/restart`, { _csrf: await alice.csrf(), back: 'items' });
    assert.equal(r.location, `/servers/${id}/items?done=itemsApplied`);
    await waitFor(async () => (await admin.query(`SELECT name FROM ${srv}.item_catalog WHERE id = 2`).catch(() => [[]]))[0][0]?.name === 'Golden Dirt', 'restart to apply the edit');
    await waitFor(async () => !/saved but not in the game yet/.test((await alice.get(`/servers/${id}/items`)).text), 'the banner to go');

    r = await alice.get(`/servers/${id}/items/6`);
    assert.match(r.text, /When worn/);
    assert.match(r.text, /Punch effect/);
    r = await alice.post(`/servers/${id}/items/6`, { _csrf: await alice.csrf(), name: 'Legendary Wings', info: '', rarity: '50', hits: '3', hitReset: '8', collision: '0',
      texture: 'tiles_page1.rttex', texX: '6', texY: '0', type: '20', maxAmount: '200', hitSound: '2', spread: '1', extraFile: 'audio/wings.wav',
      cloth: '6', punch: '7', powersShown: '1', pw_double_jump: '1', pw_speedy: '1' });
    assert.equal(r.status, 302);
    [[edit]] = await admin.query(`SELECT cloth, punch, powers, extra_file, max_amount FROM ${srv}.item_edits WHERE id = 6`);
    assert.deepEqual([edit.cloth, edit.punch, edit.powers, edit.extra_file, edit.max_amount], [6, 7, 3, 'audio/wings.wav', 200]);
    assert.equal((await alice.post(`/servers/${id}/items/6`, { _csrf: await alice.csrf(), name: 'Legendary Wings', info: '', rarity: '50', hits: '3', hitReset: '8', collision: '0',
      texture: 'tiles_page1.rttex', texX: '6', texY: '0', extraFile: '../../etc/passwd' })).status, 400);
    r = await alice.post(`/servers/${id}/items/7`, { _csrf: await alice.csrf(), name: 'Legendary Wings Seed', info: '', rarity: '50', hits: '0', hitReset: '8', collision: '0',
      texture: 'seed.rttex', texX: '0', texY: '0', grow: '3600', seedHex: '#ff8800', seedOverlayHex: '#00ff00', seedBase: '2', seedOverlay: '3', treeBase: '1', treeLeaves: '2' });
    assert.equal(r.status, 302);
    [[edit]] = await admin.query(`SELECT grow, seed_color FROM ${srv}.item_edits WHERE id = 7`);
    assert.equal(edit.grow, 3600);
    assert.equal(Number(edit.seed_color), 0x0088ffff, 'colours are 0xBBGGRRAA, keeping the alpha');
    await alice.post(`/servers/${id}/items/6/reset`, { _csrf: await alice.csrf() });
    await alice.post(`/servers/${id}/items/7/reset`, { _csrf: await alice.csrf() });
    assert.equal((await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Bad|Name', info: '', rarity: '1', hits: '1', hitReset: '1', collision: '1', texture: 'a.rttex', texX: '0', texY: '0' })).status, 400);
    assert.equal((await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Ok', info: '', rarity: '1', hits: '1', hitReset: '1', collision: '1', texture: '../x.rttex', texX: '0', texY: '0' })).status, 400);

    const found = JSON.parse((await alice.get(`/servers/${id}/items/search.json?q=wings`)).text);
    assert.equal(found.items[0].id, 6);
    r = await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '7', name: 'Emerald Wings' });
    assert.equal(r.status, 302);
    const newId = Number(r.location.match(/items\/(\d+)/)[1]);
    assert.equal(newId, 12, 'first even id after the game\'s items');
    const [made] = await admin.query(`SELECT id, base_id, name FROM ${srv}.item_edits WHERE base_id IS NOT NULL ORDER BY id`);
    assert.deepEqual(made.map((m) => [m.id, m.base_id, m.name]), [[12, 6, 'Emerald Wings'], [13, 7, 'Emerald Wings Seed']], 'a seed picked the block, and the seed came too');
    assert.match((await alice.get(`/servers/${id}/items?filter=custom`)).text, /Emerald Wings/);
    assert.equal((await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '6', name: 'emerald wings' })).status, 400, 'names are unique');
    assert.equal((await alice.post(`/servers/${id}/items`, { _csrf: await alice.csrf(), base: '12', name: 'Copy Of Copy' })).status, 400, 'only from the game\'s own items');

    r = await alice.post(`/servers/${id}/items/2/reset`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    assert.equal((await admin.query(`SELECT * FROM ${srv}.item_edits WHERE id = 2`))[0].length, 0, 'reset removes the edit');
    r = await alice.post(`/servers/${id}/items/12/reset`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [gone] = await admin.query(`SELECT removed FROM ${srv}.item_edits WHERE id IN (12, 13)`);
    assert.ok(gone.every((g) => g.removed === 1), 'deleting a custom item removes its seed too');
    assert.equal((await alice.get(`/servers/${id}/items/12`)).status, 404);

    const [sent] = await admin.query(`SELECT done_at FROM ${srv}.panel_actions WHERE action = 'items_reload'`);
    assert.equal(sent.length, 0, 'changes wait for a restart now');
    assert.equal((await alice.get(`/servers/${id}/items/2/icon.png`)).status, 404, 'no textures on this machine: the page shows a letter instead');
  });

  await t.test('machine capacities: change a machine, make a custom magplant', async () => {
    const srv = `srv_${id}`;
    let r = await alice.get(`/servers/${id}/machines`);
    assert.equal(r.status, 200);
    assert.match(r.text, /MAGPLANT 5000[\s\S]*Magplant[\s\S]*5,000[\s\S]*Any item[\s\S]*Yes/, 'the game\'s magplant with its numbers');
    assert.match(r.text, /Vending Machine[\s\S]*No limit/, 'vending machines have no limit by default');
    assert.doesNotMatch(r.text, /Legendary Wings/, 'only machines are listed');
    assert.match((await alice.get(`/servers/${id}/machines?q=vend`)).text, /1 machine</);
    assert.match((await alice.get(`/servers/${id}/machines?sort=capacity&dir=desc`)).text, /Vending Machine[\s\S]*MAGPLANT 5000/, 'no limit sorts as the biggest');
    assert.match((await alice.get(`/servers/${id}/machines?sort=capacity`)).text, /MAGPLANT 5000[\s\S]*Vending Machine/);

    r = await alice.post(`/servers/${id}/machines/8`, { _csrf: await alice.csrf(), capacity: '20,000', accepts: '1', note: 'Bigger' });
    assert.equal(r.status, 302, r.text.slice(0, 400));
    let [[row]] = await admin.query(`SELECT * FROM ${srv}.machines WHERE item = 8`);
    assert.deepEqual([row.capacity, row.accepts, row.remote, row.note], [20000, 1, 0, 'Bigger'], 'unticked building mode switches it off');
    assert.match((await alice.get(`/servers/${id}/machines`)).text, /20,000[\s\S]*Seeds only[\s\S]*Changed|Changed[\s\S]*20,000/);
    r = await alice.post(`/servers/${id}/machines/8/reset`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    [[row]] = await admin.query(`SELECT * FROM ${srv}.machines WHERE item = 8`);
    assert.deepEqual([row.capacity, row.accepts, row.remote], [5000, 0, 1], 'reset to the real game\'s numbers');
    assert.equal((await alice.post(`/servers/${id}/machines/8/delete`, { _csrf: await alice.csrf() })).status, 400, 'the game\'s machines stay');

    const bad = async (item, form, why) => assert.equal((await alice.post(`/servers/${id}/machines/${item}`, { _csrf: await alice.csrf(), ...form })).status, 400, why);
    await bad(8, { capacity: '0' }, 'a magplant holds at least 1');
    await bad(8, { capacity: '1000000001' }, 'capacity is capped');
    await bad(8, { capacity: '1.5' }, 'whole numbers');
    await bad(8, { capacity: '-5' }, 'no negative numbers');
    await bad(8, { capacity: '100', accepts: '7' }, 'accepts is checked');
    await bad(8, { capacity: '100', note: 'a|b' }, 'no | in notes');
    r = await alice.post(`/servers/${id}/machines/10`, { _csrf: await alice.csrf(), capacity: '0', accepts: '1', remote: '1' });
    assert.equal(r.status, 302, 'vending machines may have no limit');
    [[row]] = await admin.query(`SELECT * FROM ${srv}.machines WHERE item = 10`);
    assert.deepEqual([row.capacity, row.accepts, row.remote], [0, 0, 0], 'magplant settings are ignored for a vending machine');
    await alice.post(`/servers/${id}/machines/10`, { _csrf: await alice.csrf(), capacity: '2500' });
    assert.equal((await alice.get(`/servers/${id}/machines/2`)).status, 404, 'Dirt is not a machine');

    r = await alice.get(`/servers/${id}/machines/new`);
    assert.equal(r.status, 200);
    assert.match(r.text, /Copy from[\s\S]*MAGPLANT 5000[\s\S]*Vending Machine/);
    r = await alice.post(`/servers/${id}/machines`, { _csrf: await alice.csrf(), name: 'MAGPLANT 100K', base: '8', capacity: '100000', accepts: '0', remote: '1' });
    assert.equal(r.status, 302, r.text.slice(0, 400));
    const mag = Number(r.location.match(/machines\/(\d+)/)[1]);
    assert.ok(mag > 11 && mag % 2 === 0, 'a new item after the game\'s own');
    const [[made]] = await admin.query(`SELECT base_id, name FROM ${srv}.item_edits WHERE id = ?`, [mag]);
    assert.deepEqual([made.base_id, made.name], [8, 'MAGPLANT 100K'], 'copied from the MAGPLANT 5000');
    [[row]] = await admin.query(`SELECT * FROM ${srv}.machines WHERE item = ?`, [mag]);
    assert.deepEqual([row.capacity, row.accepts, row.remote, row.builtin], [100000, 0, 1, 0]);
    r = await alice.get(`/servers/${id}/machines`);
    assert.match(r.text, /MAGPLANT 100K[\s\S]*Custom[\s\S]*Restart to add[\s\S]*100,000/);
    assert.match(r.text, /item change is saved but not in the game yet|item changes are saved but not in the game yet/);
    assert.match((await alice.get(`/servers/${id}/machines/${mag}`)).text, /Delete MAGPLANT 100K/);

    const badNew = async (form, why) => assert.equal((await alice.post(`/servers/${id}/machines`, { _csrf: await alice.csrf(), ...form })).status, 400, why);
    await badNew({ name: 'Copy', base: '2', capacity: '10' }, 'only copies a machine');
    await badNew({ name: 'Copy', base: String(mag), capacity: '10' }, 'only copies the game\'s machines');
    await badNew({ name: 'magplant 100k', base: '8', capacity: '10' }, 'names are unique');
    await badNew({ name: '', base: '8', capacity: '10' }, 'needs a name');
    await badNew({ name: 'Tiny', base: '8', capacity: '0' }, 'capacity is checked');

    r = await alice.post(`/servers/${id}/machines/${mag}/delete`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    const [gone] = await admin.query(`SELECT item FROM ${srv}.machines WHERE item = ?`, [mag]);
    assert.equal(gone.length, 0);
    const [removed] = await admin.query(`SELECT removed FROM ${srv}.item_edits WHERE id IN (?, ?)`, [mag, mag + 1]);
    assert.ok(removed.length === 2 && removed.every((x) => x.removed === 1), 'its item and seed are deleted too');

    const [sent] = await admin.query(`SELECT done_at FROM ${srv}.panel_actions WHERE action = 'machines_reload'`);
    assert.equal(sent.length, 6, 'the game re-read the machines after each change');
    assert.ok(sent.every((a) => a.done_at), 'the game carried them out');
    const [log] = await admin.query(`SELECT action FROM ${srv}.mod_log WHERE action LIKE 'machine%' ORDER BY id`);
    assert.deepEqual(log.map((l) => l.action), ['machine_edit', 'machine_reset', 'machine_edit', 'machine_edit', 'machine_create', 'machine_delete']);
  });

  await t.test('resources: a PNG upload becomes an .rttex players can download', async () => {
    const rttex = require('../src/rttex');
    const rgba = Buffer.alloc(64 * 32 * 4, 0);
    for (let i = 0; i < 64 * 32; i++) { rgba[i * 4] = 200; rgba[i * 4 + 3] = 255; }
    const png = rttex.png(64, 32, rgba);
    let r = await alice.post(`/servers/${id}/resources`, { _csrf: await alice.csrf(), name: 'My Sword.png', data: png.toString('base64') });
    assert.equal(r.status, 302);
    const file = decodeURIComponent(r.location.match(/file=([^&]+)/)[1]);
    assert.match(file, new RegExp(`^ps${id}_my_sword_[0-9a-f]{8}\\.rttex$`));
    const stored = path.join(tmp, 'cdn', String(id), 'cache', 'game', file);
    const img = rttex.decode(fs.readFileSync(stored));
    assert.deepEqual([img.w, img.h, img.rgba[0]], [64, 32, 200], 'saved as an .rttex the game can read');
    r = await alice.get(`/servers/${id}/resources`);
    assert.equal(r.status, 200);
    assert.match(r.text, new RegExp(file.replace('.', '\\.')));
    assert.match(r.text, /2 cells/);
    assert.equal((await alice.get(`/servers/${id}/resources/${file}/sheet.png`)).status, 200);
    assert.equal((await alice.get(`/servers/${id}/items/texture.png?file=${file}&x=1&y=0`)).status, 200, 'the item editor previews it');
    assert.equal((await alice.get(`/servers/${id}/items/texture.png?file=${file}&x=2&y=0`)).status, 404);
    r = await alice.post(`/servers/${id}/resources`, { _csrf: await alice.csrf(), name: 'again.png', data: png.toString('base64') });
    assert.match(r.location, /done=textureExisted/, 'the same picture is kept once');
    r = await alice.post(`/servers/${id}/resources`, { _csrf: await alice.csrf(), name: 'x.png', data: Buffer.from('not a picture').toString('base64') });
    assert.equal(r.status, 400);
    assert.match(r.text, /isn&#39;t a PNG|isn't a PNG/);
    assert.equal((await bob.get(`/servers/${id}/resources/${file}/sheet.png`)).status, 404, 'other owners can\'t see it');
    assert.equal((await alice.get(`/servers/${id}/resources/..%2F..%2Fx.rttex/download`)).status, 404);
    r = await alice.post(`/servers/${id}/items/2`, { _csrf: await alice.csrf(), name: 'Dirt', info: '', rarity: '1', hits: '3', hitReset: '8', collision: '1', texture: file, texX: '1', texY: '0' });
    assert.equal(r.status, 302, 'an item can use the upload');
    assert.equal((await alice.get(`/servers/${id}/items/2/icon.png`)).status, 200);
    await alice.post(`/servers/${id}/items/2/reset`, { _csrf: await alice.csrf() });
    r = await alice.post(`/servers/${id}/resources/${file}/delete`, { _csrf: await alice.csrf() });
    assert.equal(r.status, 302);
    assert.equal(fs.existsSync(stored), false);
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
