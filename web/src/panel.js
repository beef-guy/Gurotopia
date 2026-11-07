'use strict';
/*
 * The website: owners sign up, create servers, start and stop them, and read their logs.
 * It reads and writes the platform database and asks the supervisor to run processes.
 * It never runs game code itself.
 */
const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
const express = require('express');

const { config, requireToken } = require('./config');
const db = require('./db');
const auth = require('./auth');
const servers = require('./servers');
const supervisor = require('./supervisorClient');
const hostStats = require('./hostStats');
const players = require('./players');
const worlds = require('./worlds');
const live = require('./live');
const roles = require('./roles');
const items = require('./items');
const rttex = require('./rttex');
const resources = require('./resources');

const DONE_MESSAGES = {
  created: 'Server created and starting. It shows Online once it is ready.',
  createdNotStarted: 'Server created, but it could not be started. Press Start to try again.',
  started: 'Starting. It shows Online once it is ready.',
  stopped: 'Stopped. Players and worlds were saved.',
  restarted: 'Restarted.',
  saved: 'Saved. Restart the server for the change to take effect.',
  deleted: 'Server deleted.',
  banned: 'Banned. If they were online, they were kicked off right away.',
  unbanned: 'Unbanned. They can log in again.',
  muted: 'Muted. It applies right away.',
  unmuted: 'Unmuted. They can talk again.',
  roleSet: 'Role changed. It applies right away.',
  kicked: 'Kicked.',
  worldEmptied: 'Everyone was sent out of the world.',
  worldDeleted: 'World deleted.',
  roleCreated: 'Role created. Pick its commands below, then move it up the list if it should outrank other roles.',
  roleSaved: 'Role saved. It applies right away, name colours too.',
  roleMoved: 'Order saved.',
  roleDeleted: 'Role deleted. Everyone who had it is a Player now.',
  itemSaved: 'Saved. Players see the change after they log in again.',
  itemCreated: 'Item created, with its seed. Change anything below, then players see it after they log in again.',
  textureUploaded: 'Texture uploaded. Pick it in an item\'s Texture box to use it.',
  textureExisted: 'You already had that exact picture, so the existing texture was kept.',
  textureDeleted: 'Texture deleted. Items that still use it show a blank square.',
  itemReset: 'Back to how items.dat has it. Players see it after they log in again.',
  itemDeleted: 'Custom item deleted, with its seed.',
};

function describe(server, proc) {
  // @note proc is what the supervisor knows about the process, null if it can't be reached
  if (!proc) return { key: 'unknown', label: 'Unknown', detail: 'The supervisor is not reachable.' };
  switch (proc.state) {
    case 'running':
      return server.reporting
        ? { key: 'online', label: 'Online', detail: '' }
        : { key: 'starting', label: 'Starting', detail: 'The process is up and waiting to report in.' };
    case 'stopping': return { key: 'busy', label: 'Stopping', detail: 'Saving players and worlds.' };
    case 'restarting': return { key: 'busy', label: 'Restarting', detail: proc.message };
    case 'crashed': return { key: 'crashed', label: 'Crashed', detail: proc.message };
    default: return { key: 'offline', label: 'Offline', detail: '' };
  }
}

async function processStates(ids) {
  try {
    const { servers: list } = await supervisor.all();
    const byId = new Map(list.map((p) => [p.id, p]));
    return new Map(ids.map((id) => [id, byId.get(id) || { id, state: 'stopped' }]));
  } catch (err) {
    if (err instanceof supervisor.SupervisorDown) return new Map(ids.map((id) => [id, null]));
    throw err;
  }
}

/* @note small formatters the views share */
const fmt = {
  bytes(n) {
    if (n === null || n === undefined || Number.isNaN(n)) return '–';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let i = 0;
    let v = Number(n);
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return `${v >= 100 || i === 0 ? Math.round(v) : v.toFixed(1)} ${units[i]}`;
  },
  duration(seconds) {
    if (seconds === null || seconds === undefined) return '–';
    const s = Math.max(0, Math.floor(seconds));
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d) return `${d}d ${h}h`;
    if (h) return `${h}h ${m}m`;
    return `${m}m`;
  },
  since(iso) {
    return iso ? fmt.duration((Date.now() - new Date(iso).getTime()) / 1000) : '–';
  },
  percent(n) {
    return n === null || n === undefined ? '–' : `${Number(n).toFixed(1)}%`;
  },
};

function asyncRoute(fn) {
  return (req, res, next) => Promise.resolve(fn(req, res, next)).catch(next);
}

function safeNext(value) {
  return typeof value === 'string' && /^\/[A-Za-z0-9/_-]*$/.test(value) ? value : '/servers';
}

function clientIp(req) {
  return req.ip || req.socket.remoteAddress || 'unknown';
}

/* @note changes whenever a file in public/ changes, so browsers never keep an old stylesheet or script after an update */
function assetVersion() {
  const dir = path.join(__dirname, '..', 'public');
  const hash = crypto.createHash('sha1');
  for (const name of fs.readdirSync(dir).sort()) hash.update(name).update(fs.readFileSync(path.join(dir, name)));
  return hash.digest('hex').slice(0, 10);
}

function buildApp() {
  const version = assetVersion();
  const app = express();
  app.set('view engine', 'ejs');
  app.set('views', path.join(__dirname, 'views'));
  app.set('trust proxy', 'loopback'); // @note nginx on the same machine
  app.disable('x-powered-by');

  app.use((req, res, next) => {
    res.set({
      'Content-Security-Policy': "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; frame-ancestors 'none'; form-action 'self'; base-uri 'none'",
      'X-Content-Type-Options': 'nosniff',
      'Referrer-Policy': 'same-origin',
      'X-Frame-Options': 'DENY',
    });
    next();
  });
  app.use((req, res, next) => { // @note defaults so error pages render even before the session loads
    res.locals.user = null;
    res.locals.csrf = '';
    res.locals.done = '';
    res.locals.title = '';
    res.locals.config = config.panel;
    res.locals.path = req.path;
    res.locals.v = version;
    res.locals.fmt = fmt;
    next();
  });
  app.use('/static', express.static(path.join(__dirname, '..', 'public'), { maxAge: '1h' }));
  const smallForms = express.urlencoded({ extended: false, limit: '10kb' });
  const uploadForms = express.urlencoded({ extended: false, limit: '3mb' }); // @note a texture arrives as base64 (2 MB file)
  app.use((req, res, next) => (req.method === 'POST' && /^\/servers\/[0-9]+\/resources$/.test(req.path) ? uploadForms : smallForms)(req, res, next));
  app.use(auth.loadSession);
  app.use((req, res, next) => {
    res.locals.done = DONE_MESSAGES[req.query.done] || '';
    if (res.locals.done && req.query.live === '0') {
      res.locals.done += ' This server runs an older game build, so an online player only sees it after logging in again. Restart the server to update it.';
    }
    next();
  });

  const loginLimit = auth.rateLimiter({ max: 10, windowMs: 15 * 60 * 1000 });
  const signupLimit = auth.rateLimiter({ max: 5, windowMs: 60 * 60 * 1000 });

  /* ---------- pages anyone can see ---------- */

  app.get('/', (req, res) => {
    if (req.user) return res.redirect('/servers');
    res.render('home', { title: 'Host your own server' });
  });

  app.get('/signup', (req, res) => {
    if (req.user) return res.redirect('/servers');
    res.render('signup', { title: 'Sign up', error: '', values: {} });
  });

  app.post('/signup', asyncRoute(async (req, res) => {
    const { username = '', password = '', password2 = '', code = '' } = req.body;
    const values = { username, code };
    const fail = (error) => res.status(400).render('signup', { title: 'Sign up', error, values });

    if (signupLimit.tooMany(clientIp(req))) return fail('Too many sign-ups from your network. Try again in an hour.');
    if (config.panel.signupCode && code !== config.panel.signupCode) return fail('That invite code is not right.');
    if (!auth.validUsername(username)) return fail('Use 3 to 20 letters, digits or _ for the username.');
    const problem = auth.passwordProblem(password);
    if (problem) return fail(problem);
    if (password !== password2) return fail('The two passwords are different.');

    let userId;
    try {
      userId = await auth.createUser(username, password);
    } catch (err) {
      if (err.code === 'ER_DUP_ENTRY') return fail('That username is taken.');
      throw err;
    }
    signupLimit.hit(clientIp(req));
    await auth.createSession(res, userId);
    res.redirect('/servers');
  }));

  app.get('/login', (req, res) => {
    if (req.user) return res.redirect(safeNext(req.query.next));
    res.render('login', { title: 'Log in', error: '', values: {}, next: safeNext(req.query.next) });
  });

  app.post('/login', asyncRoute(async (req, res) => {
    const { username = '', password = '' } = req.body;
    const next = safeNext(req.body.next);
    const key = `${clientIp(req)}|${String(username).toLowerCase()}`;
    const fail = (error, code = 400) => res.status(code).render('login', { title: 'Log in', error, values: { username }, next });

    if (loginLimit.tooMany(key)) return fail('Too many tries. Wait 15 minutes and try again.', 429);
    const user = await auth.checkLogin(String(username), String(password));
    if (!user) {
      loginLimit.hit(key);
      return fail('Wrong username or password.');
    }
    await auth.createSession(res, user.id);
    res.redirect(next);
  }));

  /* ---------- signed-in pages ---------- */

  app.post('/logout', auth.requireUser, auth.checkCsrf, asyncRoute(async (req, res) => {
    await auth.destroySession(req, res);
    res.redirect('/');
  }));

  async function renderDashboard(req, res, { error = '', values = null, status = 200 } = {}) {
    const list = await servers.listForOwner(req.user.id);
    const procs = await processStates(list.map((s) => s.id));
    for (const s of list) { s.view = describe(s, procs.get(s.id)); s.proc = procs.get(s.id); }
    const [platform, host] = await Promise.all([servers.platformStats(), hostStats.snapshot()]);
    res.status(status).render('servers', {
      title: 'Dashboard',
      list,
      platform,
      host,
      error,
      values: values || { maxPlayers: config.panel.defaultMaxPlayers },
      canCreate: list.length < config.panel.serversPerUser,
    });
  }

  app.get('/servers', auth.requireUser, asyncRoute(async (req, res) => renderDashboard(req, res)));

  /* @note the dashboard's live tiles poll this */
  app.get('/dashboard.json', auth.requireUser, asyncRoute(async (req, res) => {
    const [platform, host] = await Promise.all([servers.platformStats(), hostStats.snapshot()]);
    res.json({ platform, host });
  }));

  app.post('/servers', auth.requireUser, auth.checkCsrf, asyncRoute(async (req, res) => {
    const { name = '', maxPlayers = '' } = req.body;
    try {
      const server = await servers.create(req.user.id, String(name).trim(), maxPlayers);
      // @note start it straight away: its first start creates its database tables
      let done = 'created';
      try { await supervisor.start(server.id); } catch (err) { console.error(`server ${server.id}: first start failed: ${err.message}`); done = 'createdNotStarted'; }
      res.redirect(`/servers/${server.id}?done=${done}`);
    } catch (err) {
      if (!(err instanceof servers.UserError)) throw err;
      await renderDashboard(req, res, { error: err.message, values: { name, maxPlayers }, status: 400 });
    }
  }));

  /* @note loads req.server, only for its owner or an admin. anyone else gets a 404 */
  const loadServer = asyncRoute(async (req, res, next) => {
    const id = /^[1-9][0-9]{0,9}$/.test(req.params.id) ? Number(req.params.id) : null;
    const server = id ? await servers.get(id) : null;
    if (!server || (server.ownerId !== req.user.id && !req.user.isAdmin)) {
      return res.status(404).render('error', { title: 'Not found', message: 'That server does not exist, or it is not yours.' });
    }
    req.server = server;
    next();
  });

  async function statusPayload(server) {
    let proc = null;
    try { proc = await supervisor.status(server.id); } catch (err) { if (!(err instanceof supervisor.SupervisorDown)) throw err; }
    const fresh = await servers.get(server.id);
    return { server: fresh, proc, view: describe(fresh, proc) };
  }

  /* @note the pages in a server's sidebar. the ones with soon: true are on the roadmap and not clickable yet */
  const SERVER_NAV = [
    { group: 'General', items: [
      { page: 'overview', label: 'Overview', icon: 'home', path: '' },
      { page: 'console', label: 'Console', icon: 'terminal', path: '/console' },
      { label: 'Team members', icon: 'users', soon: true },
    ] },
    { group: 'Game data', items: [
      { page: 'players', label: 'Players', icon: 'user', path: '/players' },
      { page: 'worlds', label: 'Worlds', icon: 'globe', path: '/worlds' },
      { page: 'items', label: 'Item manager', icon: 'box', path: '/items' },
      { page: 'resources', label: 'Resources', icon: 'image', path: '/resources' },
      { label: 'Gem shop', icon: 'grid', soon: true },
    ] },
    { group: 'Development', items: [
      { label: 'Lua scripts', icon: 'code', soon: true },
      { page: 'roles', label: 'Roles', icon: 'shield', path: '/roles' },
    ] },
    { group: 'Settings', items: [
      { page: 'settings', label: 'Configuration', icon: 'sliders', path: '/settings' },
    ] },
  ];

  async function renderServer(req, res, page, { error = '', status = 200, extra = {}, template = null } = {}) {
    const { server, proc, view } = await statusPayload(req.server);
    res.status(status).render(`server-${template || page}`, {
      title: extra.title || server.name, server, proc, view, error, page, nav: SERVER_NAV, ...extra,
    });
  }

  app.get('/servers/:id', auth.requireUser, loadServer, asyncRoute((req, res) => renderServer(req, res, 'overview')));
  app.get('/servers/:id/console', auth.requireUser, loadServer, asyncRoute((req, res) => renderServer(req, res, 'console')));
  app.get('/servers/:id/settings', auth.requireUser, loadServer, asyncRoute((req, res) => renderServer(req, res, 'settings')));

  /* ---------- players ---------- */

  app.get('/servers/:id/players', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const query = {
      q: String(req.query.q || ''),
      sort: Object.hasOwn(players.SORTS, req.query.sort) ? req.query.sort : 'newest',
      filter: ['all', 'staff', 'banned', 'muted'].includes(req.query.filter) ? req.query.filter : 'all',
      page: req.query.page,
    };
    const [stats, result, onlineNow, roleMap] = await Promise.all([players.summary(req.server), players.list(req.server, query), live.online(req.server), roles.byId(req.server)]);
    await renderServer(req, res, 'players', { extra: { stats, result, query, onlineNow, roleMap, title: `Players · ${req.server.name}` } });
  }));

  /* @note loads req.player from this server's own database */
  const loadPlayer = asyncRoute(async (req, res, next) => {
    const uid = /^[1-9][0-9]{0,9}$/.test(req.params.uid) ? Number(req.params.uid) : null;
    const player = uid ? await players.get(req.server, uid) : null;
    if (!player) return res.status(404).render('error', { title: 'Not found', message: 'That player does not exist on this server.' });
    req.player = player;
    next();
  });

  async function renderPlayer(req, res, { error = '', status = 200 } = {}) {
    const player = await players.get(req.server, req.player.uid);
    const [more, onlineNow, roleList] = await Promise.all([players.details(req.server, player), live.online(req.server), roles.list(req.server)]);
    await renderServer(req, res, 'players', {
      error, status, template: 'player',
      extra: { player, more, roles: roleList.roles, roleMap: new Map(roleList.roles.map((r) => [r.id, r])), presence: onlineNow.get(player.growid.toLowerCase()) || null, title: `${player.growid} · ${req.server.name}` },
    });
  }

  app.get('/servers/:id/players/:uid', auth.requireUser, loadServer, loadPlayer, asyncRoute((req, res) => renderPlayer(req, res)));

  app.post('/servers/:id/players/:uid/:action(ban|unban|mute|unmute|role|kick)', auth.requireUser, loadServer, auth.checkCsrf, loadPlayer, asyncRoute(async (req, res) => {
    let result;
    try {
      result = await players.moderate(req.server, req.player, req.user.username, req.params.action, req.body);
    } catch (err) {
      if (!(err instanceof players.UserError)) throw err;
      return renderPlayer(req, res, { error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/players/${req.player.uid}?done=${result.done}${result.live ? '' : '&live=0'}`);
  }));

  /* ---------- worlds ---------- */

  app.get('/servers/:id/worlds', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const query = {
      q: String(req.query.q || ''),
      sort: Object.hasOwn(worlds.SORTS, req.query.sort) ? req.query.sort : 'name',
      filter: ['all', 'active', 'locked'].includes(req.query.filter) ? req.query.filter : 'all',
      page: req.query.page,
    };
    const result = await worlds.list(req.server, query);
    await renderServer(req, res, 'worlds', { extra: { result, query, title: `Worlds · ${req.server.name}` } });
  }));

  const loadWorld = asyncRoute(async (req, res, next) => {
    const world = await worlds.get(req.server, String(req.params.name || ''));
    if (!world) return res.status(404).render('error', { title: 'Not found', message: 'That world does not exist on this server.' });
    req.world = world;
    next();
  });

  async function renderWorld(req, res, { error = '', status = 200 } = {}) {
    const world = await worlds.get(req.server, req.world.name) || req.world;
    await renderServer(req, res, 'worlds', { error, status, template: 'world', extra: { world, title: `${world.name} · ${req.server.name}` } });
  }

  app.get('/servers/:id/worlds/:name', auth.requireUser, loadServer, loadWorld, asyncRoute((req, res) => renderWorld(req, res)));

  app.post('/servers/:id/worlds/:name/:action(empty|delete)', auth.requireUser, loadServer, auth.checkCsrf, loadWorld, asyncRoute(async (req, res) => {
    let done;
    try {
      done = req.params.action === 'empty'
        ? await worlds.emptyWorld(req.server, req.world, req.user.username)
        : await worlds.deleteWorld(req.server, req.world, req.user.username, req.body.confirm);
    } catch (err) {
      if (!(err instanceof worlds.UserError)) throw err;
      return renderWorld(req, res, { error: err.message, status: 400 });
    }
    res.redirect(done === 'worldDeleted' ? `/servers/${req.server.id}/worlds?done=${done}` : `/servers/${req.server.id}/worlds/${req.world.name}?done=${done}`);
  }));

  /* ---------- items ---------- */

  app.get('/servers/:id/items', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const query = {
      q: String(req.query.q || ''),
      sort: Object.hasOwn(items.SORTS, req.query.sort) ? req.query.sort : 'id',
      filter: Object.hasOwn(items.FILTERS, req.query.filter) ? req.query.filter : 'all',
      page: req.query.page,
    };
    const [stats, result] = await Promise.all([items.summary(req.server), items.list(req.server, query)]);
    await renderServer(req, res, 'items', { extra: { stats, result, query, filters: items.FILTERS, title: `Items · ${req.server.name}` } });
  }));

  app.get('/servers/:id/items/search.json', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const result = await items.list(req.server, { q: String(req.query.q || ''), page: 1 });
    res.json({ items: result.items.filter((i) => !i.custom).slice(0, 12).map((i) => ({ id: i.id, name: i.name, type: i.typeName })) });
  }));

  function sendIcon(res, png) {
    if (!png) return res.status(404).end();
    res.set('Cache-Control', 'private, max-age=300');
    res.type('png').send(png);
  }

  /* @note any cell of any texture, for the editor's live preview */
  app.get('/servers/:id/items/texture.png', auth.requireUser, loadServer, (req, res) => {
    const x = Number(req.query.x);
    const y = Number(req.query.y);
    if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || y < 0 || x > 255 || y > 255) return res.status(400).end();
    const file = String(req.query.file || '');
    sendIcon(res, rttex.icon(resources.folderFor(config.cdn.dir, config.texturesDir, req.server, file), file, x, y));
  });

  const loadItem = asyncRoute(async (req, res, next) => {
    const id = /^[0-9]{1,5}$/.test(req.params.itemId) ? Number(req.params.itemId) : null;
    const item = id === null ? null : await items.get(req.server, id);
    if (!item) return res.status(404).render('error', { title: 'Not found', message: 'That item does not exist on this server.' });
    req.item = item;
    next();
  });

  app.get('/servers/:id/items/:itemId/icon.png', auth.requireUser, loadServer, loadItem, (req, res) => {
    sendIcon(res, rttex.icon(resources.folderFor(config.cdn.dir, config.texturesDir, req.server, req.item.texture), req.item.texture, req.item.texX, req.item.texY));
  });

  async function renderItemForm(req, res, { item = null, form = null, error = '', status = 200 } = {}) {
    const fresh = item ? (await items.get(req.server, item.id)) || item : null;
    const base = !fresh && /^[0-9]{1,5}$/.test(String((form && form.base) || req.query.base || '')) ? await items.get(req.server, Number((form && form.base) || req.query.base)) : null;
    await renderServer(req, res, 'items', {
      error, status, template: 'item',
      extra: {
        item: fresh, form: form || {}, base, ready: await items.ready(req.server), textures: await resources.list(config.cdn.dir, req.server),
        collisions: items.COLLISIONS, flags: items.FLAGS, title: `${fresh ? fresh.name : 'New item'} · ${req.server.name}`,
      },
    });
  }

  app.get('/servers/:id/items/new', auth.requireUser, loadServer, asyncRoute((req, res) => renderItemForm(req, res)));

  app.post('/servers/:id/items', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    let result;
    try {
      if (!(await items.ready(req.server))) throw new items.UserError('Start the server once (with the latest game build) so its items show up here.');
      result = await items.create(req.server, req.body, req.user.username);
    } catch (err) {
      if (!(err instanceof items.UserError)) throw err;
      return renderItemForm(req, res, { form: req.body, error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/items/${result.id}?done=itemCreated${result.live ? '' : '&live=0'}`);
  }));

  app.get('/servers/:id/items/:itemId', auth.requireUser, loadServer, loadItem, asyncRoute((req, res) => renderItemForm(req, res, { item: req.item })));

  app.post('/servers/:id/items/:itemId', auth.requireUser, loadServer, auth.checkCsrf, loadItem, asyncRoute(async (req, res) => {
    let result;
    try {
      result = await items.update(req.server, req.item, req.body, req.user.username);
    } catch (err) {
      if (!(err instanceof items.UserError)) throw err;
      return renderItemForm(req, res, { item: req.item, form: req.body, error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/items/${req.item.id}?done=itemSaved${result.live ? '' : '&live=0'}`);
  }));

  app.post('/servers/:id/items/:itemId/reset', auth.requireUser, loadServer, auth.checkCsrf, loadItem, asyncRoute(async (req, res) => {
    const result = await items.reset(req.server, req.item, req.user.username);
    const done = req.item.custom ? 'itemDeleted' : 'itemReset';
    res.redirect(req.item.custom
      ? `/servers/${req.server.id}/items?filter=custom&done=${done}${result.live ? '' : '&live=0'}`
      : `/servers/${req.server.id}/items/${req.item.id}?done=${done}${result.live ? '' : '&live=0'}`);
  }));

  /* ---------- resources (custom textures) ---------- */

  async function renderResources(req, res, { error = '', status = 200 } = {}) {
    const files = await resources.list(config.cdn.dir, req.server);
    const picked = files.find((f) => f.file === req.query.file) || files[0] || null;
    await renderServer(req, res, 'resources', {
      error, status,
      extra: {
        files, picked, cdnReady: Boolean(config.cdn.host), maxFiles: resources.MAX_FILES,
        totalBytes: files.reduce((n, f) => n + f.bytes, 0), title: `Resources · ${req.server.name}`,
      },
    });
  }

  app.get('/servers/:id/resources', auth.requireUser, loadServer, asyncRoute((req, res) => renderResources(req, res)));

  app.post('/servers/:id/resources', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    let result;
    try {
      const b64 = String(req.body.data || '');
      if (!/^[A-Za-z0-9+/]*={0,2}$/.test(b64)) throw new resources.UserError('That upload was damaged. Try again.');
      result = await resources.upload(config.cdn.dir, req.server, String(req.body.name || ''), Buffer.from(b64, 'base64'));
    } catch (err) {
      if (!(err instanceof resources.UserError)) throw err;
      return renderResources(req, res, { error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/resources?file=${encodeURIComponent(result.file)}&done=${result.existed ? 'textureExisted' : 'textureUploaded'}`);
  }));

  app.get('/servers/:id/resources/:file/sheet.png', auth.requireUser, loadServer, (req, res) => {
    if (!resources.isOwnName(req.server, req.params.file)) return res.status(404).end();
    sendIcon(res, rttex.sheetPng(resources.dirFor(config.cdn.dir, req.server), req.params.file));
  });

  app.get('/servers/:id/resources/:file/download', auth.requireUser, loadServer, (req, res) => {
    if (!resources.exists(config.cdn.dir, req.server, req.params.file)) return res.status(404).end();
    res.download(path.join(resources.dirFor(config.cdn.dir, req.server), req.params.file), req.params.file);
  });

  app.post('/servers/:id/resources/:file/delete', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    try { await resources.remove(config.cdn.dir, req.server, req.params.file); } catch (err) {
      if (!(err instanceof resources.UserError)) throw err;
      return renderResources(req, res, { error: err.message, status: 404 });
    }
    res.redirect(`/servers/${req.server.id}/resources?done=textureDeleted`);
  }));

  /* ---------- roles ---------- */

  app.get('/servers/:id/roles', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const [result, groups] = await Promise.all([roles.list(req.server), roles.catalog(req.server)]);
    const total = groups.reduce((n, g) => n + g.commands.length, 0);
    await renderServer(req, res, 'roles', { extra: { result, total, title: `Roles · ${req.server.name}` } });
  }));

  /* @note loads req.role. "new" is the form for a new role */
  const loadRole = asyncRoute(async (req, res, next) => {
    const id = /^[0-9]{1,6}$/.test(req.params.rid) ? Number(req.params.rid) : null;
    const role = id === null ? null : await roles.get(req.server, id);
    if (!role) return res.status(404).render('error', { title: 'Not found', message: 'That role does not exist on this server.' });
    req.role = role;
    next();
  });

  async function renderRole(req, res, { role = null, form = null, error = '', status = 200 } = {}) {
    const [result, groups] = await Promise.all([roles.list(req.server), roles.catalog(req.server)]);
    const fresh = role ? (result.roles.find((r) => r.id === role.id) || role) : null;
    const shown = form ? {
      ...(fresh || {}), name: String(form.name || ''), color: String(form.color || ''), tag: String(form.tag || ''),
      powers: Number(form.powers), commands: [].concat(form.commands || []).map(String),
    } : (fresh || { name: '', color: 'e', tag: '', powers: roles.PLAYER, commands: [], builtin: false, members: 0 });
    await renderServer(req, res, 'roles', {
      error, status, template: 'role',
      extra: { role: fresh, shown, ready: result.ready, groups, colors: roles.COLORS, powers: roles.POWERS, title: `${fresh ? fresh.name : 'New role'} · ${req.server.name}` },
    });
  }

  app.get('/servers/:id/roles/new', auth.requireUser, loadServer, asyncRoute((req, res) => renderRole(req, res)));

  app.post('/servers/:id/roles', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    let result;
    try {
      if (!(await roles.list(req.server)).ready) throw new roles.UserError('Start the server once (with the latest game build) before making roles.');
      result = await roles.create(req.server, req.body, req.user.username);
    } catch (err) {
      if (!(err instanceof roles.UserError)) throw err;
      return renderRole(req, res, { form: req.body, error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/roles/${result.id}?done=roleCreated${result.live ? '' : '&live=0'}`);
  }));

  app.get('/servers/:id/roles/:rid', auth.requireUser, loadServer, loadRole, asyncRoute((req, res) => renderRole(req, res, { role: req.role })));

  app.post('/servers/:id/roles/:rid', auth.requireUser, loadServer, auth.checkCsrf, loadRole, asyncRoute(async (req, res) => {
    let result;
    try {
      result = await roles.update(req.server, req.role, req.body, req.user.username);
    } catch (err) {
      if (!(err instanceof roles.UserError)) throw err;
      return renderRole(req, res, { role: req.role, form: req.body, error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/roles/${req.role.id}?done=roleSaved${result.live ? '' : '&live=0'}`);
  }));

  app.post('/servers/:id/roles/:rid/:action(up|down|delete)', auth.requireUser, loadServer, auth.checkCsrf, loadRole, asyncRoute(async (req, res) => {
    let result;
    try {
      result = req.params.action === 'delete'
        ? await roles.remove(req.server, req.role, req.user.username)
        : await roles.move(req.server, req.role, req.params.action, req.user.username);
    } catch (err) {
      if (!(err instanceof roles.UserError)) throw err;
      return renderRole(req, res, { role: req.role, error: err.message, status: 400 });
    }
    const done = req.params.action === 'delete' ? 'roleDeleted' : 'roleMoved';
    res.redirect(`/servers/${req.server.id}/roles?done=${done}${result.live ? '' : '&live=0'}`);
  }));

  app.get('/servers/:id/status.json', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const { server, proc, view } = await statusPayload(req.server);
    res.json({
      view,
      online: server.online,
      maxPlayers: server.maxPlayers,
      startedAt: proc?.startedAt || null,
      lastSeen: server.lastSeen,
      cpu: proc?.cpu ?? null,
      mem: proc?.mem ?? null,
    });
  }));

  const RANGES = new Set([15, 30, 60, 180, 360]);
  app.get('/servers/:id/metrics.json', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const minutes = RANGES.has(Number(req.query.minutes)) ? Number(req.query.minutes) : 30;
    try {
      res.json(await supervisor.metrics(req.server.id, minutes));
    } catch (err) {
      if (!(err instanceof supervisor.SupervisorDown)) throw err;
      res.status(503).json({ samples: [], storage: null, error: err.message });
    }
  }));

  app.get('/servers/:id/logs.json', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    try {
      const { text } = await supervisor.logs(req.server.id, 30000);
      res.json({ text });
    } catch (err) {
      if (!(err instanceof supervisor.SupervisorDown)) throw err;
      res.status(503).json({ text: '', error: err.message });
    }
  }));

  const actions = {
    start: { run: supervisor.start, done: 'started' },
    stop: { run: supervisor.stop, done: 'stopped' },
    restart: { run: supervisor.restart, done: 'restarted' },
  };
  for (const [name, action] of Object.entries(actions)) {
    app.post(`/servers/:id/${name}`, auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
      try {
        await action.run(req.server.id);
      } catch (err) {
        return renderServer(req, res, 'overview', { error: `Could not ${name} the server: ${err.message}`, status: 502 });
      }
      res.redirect(`/servers/${req.server.id}?done=${action.done}`);
    }));
  }

  app.post('/servers/:id/settings', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    try {
      await servers.updateMaxPlayers(req.server.id, req.body.maxPlayers);
    } catch (err) {
      if (!(err instanceof servers.UserError)) throw err;
      return renderServer(req, res, 'settings', { error: err.message, status: 400 });
    }
    res.redirect(`/servers/${req.server.id}/settings?done=saved`);
  }));

  app.post('/servers/:id/delete', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    if (String(req.body.confirm || '') !== req.server.name) {
      return renderServer(req, res, 'settings', { error: 'Type the server name exactly to delete it.', status: 400 });
    }
    try {
      await supervisor.remove(req.server.id); // @note stops it and deletes its folder
    } catch (err) {
      return renderServer(req, res, 'settings', { error: `Could not delete the server: ${err.message}`, status: 502 });
    }
    await servers.destroy(req.server);
    res.redirect(req.user.isAdmin && req.server.ownerId !== req.user.id ? '/admin?done=deleted' : '/servers?done=deleted');
  }));

  /* ---------- platform admin ---------- */

  app.get('/admin', auth.requireUser, auth.requireAdmin, asyncRoute(async (req, res) => {
    const list = await servers.listAll();
    const procs = await processStates(list.map((s) => s.id));
    for (const s of list) s.view = describe(s, procs.get(s.id));
    const [[{ users }]] = await db.getPool().query('SELECT COUNT(*) AS users FROM panel_users');
    res.render('admin', { title: 'Admin', list, users: Number(users) });
  }));

  /* ---------- errors ---------- */

  app.use((req, res) => res.status(404).render('error', { title: 'Not found', message: 'That page does not exist.' }));
  // eslint-disable-next-line no-unused-vars
  app.use((err, req, res, next) => {
    console.error(err);
    res.status(500).render('error', { title: 'Something broke', message: 'Something went wrong on our side. Try again in a moment.' });
  });

  return app;
}

async function main() {
  requireToken();
  await db.connect();
  await auth.cleanupSessions();
  hostStats.startSampling();
  setInterval(() => auth.cleanupSessions().catch((err) => console.error(err)), 3600 * 1000).unref();

  const app = buildApp();
  app.listen(config.panel.port, config.panel.host, () => {
    console.log(`panel listening on http://${config.panel.host}:${config.panel.port}`);
  });
}

if (require.main === module) {
  main().catch((err) => {
    console.error(err.message);
    process.exit(1);
  });
}

module.exports = { buildApp, describe, fmt };
