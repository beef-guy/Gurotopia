'use strict';
/*
 * The supervisor starts, stops and restarts game servers, one process each,
 * and keeps their logs. It only listens on 127.0.0.1 and every request needs
 * SUPERVISOR_TOKEN, so only the panel on the same machine can use it.
 *
 * It runs as its own service so that restarting or updating the website
 * never takes the game servers down.
 */
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');

const { config, requireToken } = require('./config');
const db = require('./db');

const S = config.supervisor;
const SECRET_ENV = ['DB_PASSWORD', 'SUPERVISOR_TOKEN', 'SIGNUP_CODE'];
const STDBUF = ['/usr/bin/stdbuf', '/bin/stdbuf'].find((p) => fs.existsSync(p));

/* ---------- logs ---------- */

class LogFile {
  constructor(file, maxBytes) {
    this.file = file;
    this.maxBytes = maxBytes;
    this.fd = null;
    this.size = 0;
    this.partial = { stdout: '', stderr: '' };
  }

  open() {
    if (this.fd !== null) return;
    fs.mkdirSync(path.dirname(this.file), { recursive: true });
    this.fd = fs.openSync(this.file, 'a', 0o640);
    this.size = fs.fstatSync(this.fd).size;
  }

  close() {
    for (const key of Object.keys(this.partial)) {
      if (this.partial[key]) this.writeLine(this.partial[key]);
      this.partial[key] = '';
    }
    if (this.fd !== null) fs.closeSync(this.fd);
    this.fd = null;
  }

  rotate() {
    fs.closeSync(this.fd);
    fs.renameSync(this.file, `${this.file}.1`);
    this.fd = fs.openSync(this.file, 'a', 0o640);
    this.size = 0;
  }

  writeLine(text) {
    if (this.fd === null) this.open();
    const stamp = new Date().toISOString().replace('T', ' ').slice(0, 19);
    const buf = Buffer.from(`[${stamp}] ${text}\n`);
    if (this.size + buf.length > this.maxBytes && this.size > 0) this.rotate();
    fs.writeSync(this.fd, buf);
    this.size += buf.length;
  }

  /* @note game output arrives in chunks that can split a line */
  feed(stream, chunk) {
    const text = this.partial[stream] + chunk.toString('utf8');
    const lines = text.split(/\r?\n/);
    this.partial[stream] = lines.pop();
    for (const line of lines) this.writeLine(stream === 'stderr' ? `[err] ${line}` : line);
    if (this.partial[stream].length > 8192) { // @note never buffer forever
      this.writeLine(this.partial[stream]);
      this.partial[stream] = '';
    }
  }

  note(text) { this.writeLine(`[supervisor] ${text}`); }
}

async function readTail(file, bytes) {
  let handle;
  try {
    handle = await fsp.open(file, 'r');
  } catch (err) {
    if (err.code === 'ENOENT') return '';
    throw err;
  }
  try {
    const { size } = await handle.stat();
    const length = Math.min(size, bytes);
    const buf = Buffer.alloc(length);
    await handle.read(buf, 0, length, size - length);
    let text = buf.toString('utf8');
    if (length < size) text = text.slice(text.indexOf('\n') + 1); // @note drop the cut first line
    return text;
  } finally {
    await handle.close();
  }
}

/* ---------- servers ---------- */

function serverDir(id) {
  if (!Number.isInteger(id) || id <= 0) throw new Error(`bad server id ${id}`);
  return path.join(S.serversDir, String(id));
}

const entries = new Map(); // @note id -> entry

function entryFor(id) {
  let entry = entries.get(id);
  if (!entry) {
    entry = {
      id,
      state: 'stopped', // @note stopped | running | stopping | restarting | crashed
      child: null,
      pid: null,
      startedAt: null,
      crashes: [],
      restartTimer: null,
      stopTimer: null,
      stopping: false,
      launching: null,
      waiters: [],
      lastExit: null,
      message: '',
      log: new LogFile(path.join(serverDir(id), 'logs', 'server.log'), S.logMaxBytes),
    };
    entries.set(id, entry);
  }
  return entry;
}

function publicState(entry) {
  return {
    id: entry.id,
    state: entry.state,
    pid: entry.pid,
    startedAt: entry.startedAt,
    recentCrashes: entry.crashes.length,
    lastExit: entry.lastExit,
    message: entry.message,
  };
}

async function loadServer(id) {
  const [rows] = await db.getPool().query(
    'SELECT id, name, port, db_name, max_players FROM servers WHERE id = ?', [id]);
  return rows[0] || null;
}

async function setWantRunning(id, want) {
  await db.getPool().query(
    `INSERT INTO server_control (server_id, want_running) VALUES (?, ?)
     ON DUPLICATE KEY UPDATE want_running = VALUES(want_running)`, [id, want ? 1 : 0]);
}

/* @note the game sets 'running' itself. after the process is gone, make sure the gateway stops sending players there */
async function markStopped(id) {
  try {
    await db.getPool().query("UPDATE servers SET status = 'stopped', online = 0 WHERE id = ?", [id]);
  } catch (err) {
    console.error(`server ${id}: could not mark stopped: ${err.message}`);
  }
}

async function prepareFolder(id) {
  const dir = serverDir(id);
  await fsp.mkdir(path.join(dir, 'logs'), { recursive: true });
  await fsp.mkdir(path.join(dir, 'scripts'), { recursive: true }); // @note Lua scripts will live here

  for (const shared of ['items.dat', 'resources']) { // @note read-only files every server uses
    const link = path.join(dir, shared);
    const target = path.join(S.gameDir, shared);
    try {
      await fsp.lstat(link);
    } catch (err) {
      if (err.code !== 'ENOENT') throw err;
      // @note Windows needs admin rights for links, so copy there (only for testing on a PC)
      if (process.platform === 'win32') await fsp.cp(target, link, { recursive: true });
      else await fsp.symlink(target, link);
    }
  }

  const login = path.join(S.gameDir, 'mysql_login.txt');
  if (fs.existsSync(login)) {
    await fsp.copyFile(login, path.join(dir, 'mysql_login.txt'));
    await fsp.chmod(path.join(dir, 'mysql_login.txt'), 0o600);
  }
  return dir;
}

function gameArgs(row) {
  return [
    '--name', row.name,
    '--port', String(row.port),
    '--database', row.db_name,
    '--platform-db', config.db.platformDb,
    '--no-https',
    '--max-players', String(row.max_players),
  ];
}

function childEnv() {
  const env = { ...process.env };
  for (const key of SECRET_ENV) delete env[key];
  return env;
}

/* @note one launch at a time per server, so two quick Start presses can't start two copies */
function launch(entry) {
  if (!entry.launching) entry.launching = doLaunch(entry).finally(() => { entry.launching = null; });
  return entry.launching;
}

async function doLaunch(entry) {
  const row = await loadServer(entry.id);
  if (!row) {
    entry.state = 'stopped';
    entry.message = 'this server no longer exists';
    return;
  }
  // @note these would fail the same way on every retry, so stop with a clear message instead
  for (const needed of [S.gameBinPath, path.join(S.gameDir, 'items.dat'), path.join(S.gameDir, 'mysql_login.txt')]) {
    if (!fs.existsSync(needed)) throw new Error(`${needed} is missing on the VPS`);
  }
  const cwd = await prepareFolder(entry.id);

  let command = S.gameBinPath;
  let args = gameArgs(row);
  if (S.lineBuffered && STDBUF) { // @note otherwise the game's output sits in a buffer and the log looks empty
    args = ['-oL', '-eL', command, ...args];
    command = STDBUF;
  }

  entry.log.note(`starting ${row.name} on port ${row.port} (database ${row.db_name}, up to ${row.max_players} players)`);
  const child = spawn(command, args, { cwd, env: childEnv(), stdio: ['ignore', 'pipe', 'pipe'] });

  entry.child = child;
  entry.pid = child.pid ?? null;
  entry.startedAt = new Date().toISOString();
  entry.state = 'running';
  entry.message = '';
  entry.stopping = false;

  child.stdout.on('data', (chunk) => entry.log.feed('stdout', chunk));
  child.stderr.on('data', (chunk) => entry.log.feed('stderr', chunk));

  let finished = false;
  const onGone = (code, signal, error) => {
    if (finished) return;
    finished = true;
    onExit(entry, child, code, signal, error).catch((err) => console.error(err));
  };
  child.on('error', (err) => onGone(null, null, err)); // @note e.g. main.out not found
  child.on('close', (code, signal) => onGone(code, signal, null));
}

async function onExit(entry, child, code, signal, error) {
  if (entry.child !== child) return;
  clearTimeout(entry.stopTimer);
  entry.child = null;
  entry.pid = null;
  entry.lastExit = { code, signal, error: error ? error.message : null, at: new Date().toISOString() };

  if (error) entry.log.note(`could not run ${S.gameBinPath}: ${error.message}`);
  else entry.log.note(`server exited (${signal ? `signal ${signal}` : `code ${code}`})`);
  await markStopped(entry.id);

  const waiters = entry.waiters.splice(0);
  if (entry.stopping) {
    entry.stopping = false;
    entry.state = 'stopped';
    entry.log.close();
    waiters.forEach((resolve) => resolve());
    return;
  }

  // @note it stopped on its own: a crash
  const now = Date.now();
  entry.crashes = entry.crashes.filter((t) => now - t < S.crashWindowMs);
  entry.crashes.push(now);

  if (error || entry.crashes.length >= S.maxCrashes) {
    entry.state = 'crashed';
    entry.message = error
      ? `could not start: ${error.message}`
      : `crashed ${entry.crashes.length} times in ${Math.round(S.crashWindowMs / 60000)} minutes, so it was not restarted. Check the log, then press Start.`;
    entry.log.note(entry.message);
    entry.log.close();
    waiters.forEach((resolve) => resolve());
    return;
  }

  const delay = Math.min(60_000, 2000 * 2 ** (entry.crashes.length - 1));
  entry.state = 'restarting';
  entry.message = `crashed, restarting in ${Math.round(delay / 1000)} s`;
  entry.log.note(entry.message);
  entry.restartTimer = setTimeout(() => {
    entry.restartTimer = null;
    if (entry.state !== 'restarting') return;
    launch(entry).catch((err) => failLaunch(entry, err));
  }, delay);
  waiters.forEach((resolve) => resolve());
}

function failLaunch(entry, err) {
  entry.state = 'crashed';
  entry.message = `could not start: ${err.message}`;
  try { entry.log.note(entry.message); entry.log.close(); } catch { /* @note folder may be gone */ }
  console.error(`server ${entry.id}: ${err.stack || err.message}`);
}

async function start(id) {
  const row = await loadServer(id);
  if (!row) return null;
  const entry = entryFor(id);
  await setWantRunning(id, true);
  if (entry.child || entry.launching) return publicState(entry); // @note already running, starting or stopping
  clearTimeout(entry.restartTimer);
  entry.restartTimer = null;
  entry.crashes = []; // @note pressing Start forgives earlier crashes
  try {
    await launch(entry);
  } catch (err) {
    failLaunch(entry, err);
  }
  return publicState(entry);
}

/* @note persist=false is for the supervisor shutting down: servers come back when it starts again */
async function stop(id, { persist = true } = {}) {
  const entry = entryFor(id);
  if (persist) await setWantRunning(id, false);
  clearTimeout(entry.restartTimer);
  entry.restartTimer = null;
  if (entry.launching) await entry.launching.catch(() => {});

  if (!entry.child) {
    if (entry.state !== 'crashed' || persist) entry.state = 'stopped';
    entry.message = '';
    return publicState(entry);
  }

  const done = new Promise((resolve) => entry.waiters.push(resolve));
  if (!entry.stopping) {
    entry.stopping = true;
    entry.state = 'stopping';
    entry.log.note('stopping (saving players and worlds)');
    entry.child.kill('SIGINT'); // @note the game saves everything on SIGINT
    entry.stopTimer = setTimeout(() => {
      if (entry.child) {
        entry.log.note(`did not stop within ${S.stopTimeoutMs / 1000} s, killing it`);
        entry.child.kill('SIGKILL');
      }
    }, S.stopTimeoutMs);
  }
  await done;
  return publicState(entry);
}

async function restart(id) {
  if (!(await loadServer(id))) return null;
  await stop(id, { persist: false });
  return start(id);
}

async function remove(id) {
  await stop(id);
  entries.delete(id);
  await db.getPool().query('DELETE FROM server_control WHERE server_id = ?', [id]);
  const dir = serverDir(id);
  if (path.dirname(dir) !== S.serversDir) throw new Error('refusing to delete outside SERVERS_DIR');
  await fsp.rm(dir, { recursive: true, force: true }); // @note symlinks are removed, not followed
  return { id, state: 'deleted' };
}

function status(id) {
  const entry = entries.get(id);
  return entry ? publicState(entry) : { id, state: 'stopped', pid: null, startedAt: null, recentCrashes: 0, lastExit: null, message: '' };
}

async function logs(id, bytes) {
  return readTail(path.join(serverDir(id), 'logs', 'server.log'), bytes);
}

async function startWanted() {
  const [rows] = await db.getPool().query(
    `SELECT s.id FROM servers s JOIN server_control c ON c.server_id = s.id WHERE c.want_running = 1`);
  for (const { id } of rows) {
    await start(id).catch((err) => console.error(`server ${id}: ${err.message}`));
  }
  return rows.length;
}

/* ---------- HTTP API ---------- */

function authorized(req) {
  const header = req.headers.authorization || '';
  const given = Buffer.from(header.startsWith('Bearer ') ? header.slice(7) : '');
  const wanted = Buffer.from(S.token);
  return given.length === wanted.length && crypto.timingSafeEqual(given, wanted);
}

function send(res, code, body) {
  const json = JSON.stringify(body);
  res.writeHead(code, { 'content-type': 'application/json', 'content-length': Buffer.byteLength(json) });
  res.end(json);
}

async function handle(req, res) {
  if (!authorized(req)) return send(res, 401, { error: 'unauthorized' });
  const url = new URL(req.url, 'http://localhost');
  const parts = url.pathname.split('/').filter(Boolean);

  if (req.method === 'GET' && url.pathname === '/health') return send(res, 200, { ok: true, servers: entries.size });
  if (req.method === 'GET' && url.pathname === '/servers') {
    return send(res, 200, { servers: [...entries.values()].map(publicState) });
  }
  if (parts[0] !== 'servers' || !/^[1-9][0-9]{0,9}$/.test(parts[1] || '')) return send(res, 404, { error: 'not found' });

  const id = Number(parts[1]);
  const action = parts[2] || '';
  let result;

  if (req.method === 'GET' && action === '') result = status(id);
  else if (req.method === 'GET' && action === 'logs') {
    const bytes = Math.min(Math.max(Number.parseInt(url.searchParams.get('bytes') || '20000', 10) || 20000, 1000), 500_000);
    result = { id, text: await logs(id, bytes) };
  }
  else if (req.method === 'POST' && action === 'start') result = await start(id);
  else if (req.method === 'POST' && action === 'stop') result = await stop(id);
  else if (req.method === 'POST' && action === 'restart') result = await restart(id);
  else if (req.method === 'DELETE' && action === '') result = await remove(id);
  else return send(res, 404, { error: 'not found' });

  if (result === null) return send(res, 404, { error: 'no such server' });
  return send(res, 200, result);
}

async function main() {
  requireToken();
  await db.connect();
  await fsp.mkdir(S.serversDir, { recursive: true });
  if (!fs.existsSync(S.gameBinPath)) console.warn(`warning: ${S.gameBinPath} does not exist yet`);
  if (S.lineBuffered && !STDBUF) console.warn('warning: stdbuf not found, server logs may show up late');

  const server = http.createServer((req, res) => {
    handle(req, res).catch((err) => {
      console.error(err);
      if (!res.headersSent) send(res, 500, { error: err.message });
    });
  });
  server.listen(S.port, S.host, () => console.log(`supervisor listening on ${S.host}:${S.port}`));

  const started = await startWanted();
  console.log(`started ${started} server(s) that were running before`);

  let shuttingDown = false;
  const shutdown = async (signal) => {
    if (shuttingDown) return;
    shuttingDown = true;
    console.log(`${signal}: stopping every server so they save`);
    server.close();
    await Promise.all([...entries.keys()].map((id) => stop(id, { persist: false }).catch((err) => console.error(err))));
    await db.close();
    process.exit(0);
  };
  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));
}

if (require.main === module) {
  main().catch((err) => {
    console.error(err.message);
    process.exit(1);
  });
}

module.exports = { LogFile, readTail, gameArgs };
