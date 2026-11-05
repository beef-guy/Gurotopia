'use strict';
/*
 * The website: owners sign up, create servers, start and stop them, and read their logs.
 * It reads and writes the platform database and asks the supervisor to run processes.
 * It never runs game code itself.
 */
const path = require('node:path');
const express = require('express');

const { config, requireToken } = require('./config');
const db = require('./db');
const auth = require('./auth');
const servers = require('./servers');
const supervisor = require('./supervisorClient');

const DONE_MESSAGES = {
  created: 'Server created and starting. It shows Online once it is ready.',
  createdNotStarted: 'Server created, but it could not be started. Press Start to try again.',
  started: 'Starting. It shows Online once it is ready.',
  stopped: 'Stopped. Players and worlds were saved.',
  restarted: 'Restarted.',
  saved: 'Saved. Restart the server for the change to take effect.',
  deleted: 'Server deleted.',
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

function asyncRoute(fn) {
  return (req, res, next) => Promise.resolve(fn(req, res, next)).catch(next);
}

function safeNext(value) {
  return typeof value === 'string' && /^\/[A-Za-z0-9/_-]*$/.test(value) ? value : '/servers';
}

function clientIp(req) {
  return req.ip || req.socket.remoteAddress || 'unknown';
}

function buildApp() {
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
    next();
  });
  app.use('/static', express.static(path.join(__dirname, '..', 'public'), { maxAge: '1h' }));
  app.use(express.urlencoded({ extended: false, limit: '10kb' }));
  app.use(auth.loadSession);
  app.use((req, res, next) => {
    res.locals.done = DONE_MESSAGES[req.query.done] || '';
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

  app.get('/servers', auth.requireUser, asyncRoute(async (req, res) => {
    const list = await servers.listForOwner(req.user.id);
    const procs = await processStates(list.map((s) => s.id));
    for (const s of list) s.view = describe(s, procs.get(s.id));
    res.render('servers', {
      title: 'Your servers',
      list,
      error: '',
      values: { maxPlayers: config.panel.defaultMaxPlayers },
      canCreate: list.length < config.panel.serversPerUser,
    });
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
      const list = await servers.listForOwner(req.user.id);
      const procs = await processStates(list.map((s) => s.id));
      for (const s of list) s.view = describe(s, procs.get(s.id));
      res.status(400).render('servers', {
        title: 'Your servers',
        list,
        error: err.message,
        values: { name, maxPlayers },
        canCreate: list.length < config.panel.serversPerUser,
      });
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

  app.get('/servers/:id', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const { server, proc, view } = await statusPayload(req.server);
    res.render('server', { title: server.name, server, proc, view, error: '' });
  }));

  app.get('/servers/:id/status.json', auth.requireUser, loadServer, asyncRoute(async (req, res) => {
    const { server, proc, view } = await statusPayload(req.server);
    res.json({
      view,
      online: server.online,
      maxPlayers: server.maxPlayers,
      startedAt: proc?.startedAt || null,
      lastSeen: server.lastSeen,
    });
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
        const { server, proc, view } = await statusPayload(req.server);
        return res.status(502).render('server', { title: server.name, server, proc, view, error: `Could not ${name} the server: ${err.message}` });
      }
      res.redirect(`/servers/${req.server.id}?done=${action.done}`);
    }));
  }

  app.post('/servers/:id/settings', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    try {
      await servers.updateMaxPlayers(req.server.id, req.body.maxPlayers);
    } catch (err) {
      if (!(err instanceof servers.UserError)) throw err;
      const { server, proc, view } = await statusPayload(req.server);
      return res.status(400).render('server', { title: server.name, server, proc, view, error: err.message });
    }
    res.redirect(`/servers/${req.server.id}?done=saved`);
  }));

  app.post('/servers/:id/delete', auth.requireUser, loadServer, auth.checkCsrf, asyncRoute(async (req, res) => {
    if (String(req.body.confirm || '') !== req.server.name) {
      const { server, proc, view } = await statusPayload(req.server);
      return res.status(400).render('server', { title: server.name, server, proc, view, error: 'Type the server name exactly to delete it.' });
    }
    try {
      await supervisor.remove(req.server.id); // @note stops it and deletes its folder
    } catch (err) {
      const { server, proc, view } = await statusPayload(req.server);
      return res.status(502).render('server', { title: server.name, server, proc, view, error: `Could not delete the server: ${err.message}` });
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

module.exports = { buildApp, describe };
