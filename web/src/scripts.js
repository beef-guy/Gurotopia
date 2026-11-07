'use strict';
/*
 * A server's Lua scripts, in the lua_scripts table of its own database. The game (include/scripting/lua.cpp)
 * makes the same table on start; the website makes it too, so owners can write scripts before the first start.
 *
 * - mode 'code': the owner wrote the Lua (source)
 * - mode 'blocks': the block editor's workspace is in blocks (JSON), and source is the Lua it made
 *
 * Every change asks a running game to load all scripts again (scripts_reload), which takes under a second.
 * The game writes what scripts print, and their errors, to lua_log.
 */
const db = require('./db');
const live = require('./live');

const MAX_SCRIPTS = 50;
const MAX_SOURCE = 256 * 1024;
const MAX_BLOCKS = 1024 * 1024;
const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9 _.-]{0,47}$/;

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR');
}

/* @note keep these the same as lua::init() in include/scripting/lua.cpp */
const SCHEMA = {
  lua_scripts: `(
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(48) NOT NULL,
    enabled TINYINT NOT NULL DEFAULT 1,
    mode VARCHAR(8) NOT NULL DEFAULT 'code',
    source MEDIUMTEXT,
    blocks MEDIUMTEXT,
    sort INT NOT NULL DEFAULT 0,
    updated_by VARCHAR(24) DEFAULT '',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
  )`,
  lua_log: `(
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    script VARCHAR(48) NOT NULL,
    level VARCHAR(8) NOT NULL,
    message VARCHAR(1000) NOT NULL
  )`,
};

const ready = new Set();
async function ensure(server) {
  if (ready.has(server.dbName)) return;
  const pool = db.getPool();
  // @note the website makes srv_<id> databases. any other (an older test server) must already be there
  if (/^srv_[0-9]+$/.test(server.dbName)) await pool.query(`CREATE DATABASE IF NOT EXISTS ${db.quoteIdent(server.dbName)}`);
  for (const [name, columns] of Object.entries(SCHEMA)) await pool.query(`CREATE TABLE IF NOT EXISTS ${table(server, name)} ${columns}`);
  ready.add(server.dbName);
}

function shape(row) {
  return {
    id: Number(row.id),
    name: row.name,
    enabled: Number(row.enabled) === 1,
    mode: row.mode === 'blocks' ? 'blocks' : 'code',
    source: row.source || '',
    blocks: row.blocks || '',
    sort: Number(row.sort || 0),
    updatedBy: row.updated_by || '',
    updatedAt: row.updated_at,
    lines: row.source ? String(row.source).split('\n').length : 0,
  };
}

async function list(server) {
  await ensure(server);
  const [rows] = await db.getPool().query(
    `SELECT id, name, enabled, mode, source, '' AS blocks, sort, updated_by, updated_at FROM ${table(server, 'lua_scripts')} ORDER BY sort, id`);
  return rows.map(shape);
}

async function get(server, id) {
  await ensure(server);
  const [[row]] = await db.getPool().query(`SELECT * FROM ${table(server, 'lua_scripts')} WHERE id = ?`, [id]);
  return row ? shape(row) : null;
}

async function tellGame(server) {
  const id = await live.queue(server, 'scripts_reload', '-');
  if (id) await live.waitDone(server, id, 3000);
  return { live: !server.reporting || Boolean(id), running: Boolean(server.reporting) };
}

function readName(value) {
  const name = String(value || '').trim().replace(/\s+/g, ' ');
  if (!NAME_RE.test(name)) throw new UserError('Give the script a name of up to 48 letters, numbers, spaces, dots, dashes or _.');
  return name;
}

async function create(server, { name, mode, source = '', blocks = '' }, actor) {
  const scripts = await list(server);
  if (scripts.length >= MAX_SCRIPTS) throw new UserError(`A server can have up to ${MAX_SCRIPTS} scripts.`);
  const clean = readName(name);
  if (scripts.some((s) => s.name.toLowerCase() === clean.toLowerCase())) throw new UserError('There is already a script with that name.');
  const kind = mode === 'blocks' ? 'blocks' : 'code';
  if (source.length > MAX_SOURCE || blocks.length > MAX_BLOCKS) throw new UserError('That script is too big.');
  const sort = scripts.reduce((n, s) => Math.max(n, s.sort), 0) + 1;
  const [result] = await db.getPool().query(
    `INSERT INTO ${table(server, 'lua_scripts')} (name, enabled, mode, source, blocks, sort, updated_by) VALUES (?, 1, ?, ?, ?, ?, ?)`,
    [clean, kind, source, blocks, sort, String(actor).slice(0, 24)]);
  const told = await tellGame(server);
  return { id: result.insertId, ...told };
}

/* @note form: name, source, blocks (blocks mode), mode ('code' turns a blocks script into code for good) */
async function update(server, script, form, actor) {
  const name = form.name === undefined ? script.name : readName(form.name);
  if (name.toLowerCase() !== script.name.toLowerCase() && (await list(server)).some((s) => s.id !== script.id && s.name.toLowerCase() === name.toLowerCase())) {
    throw new UserError('There is already a script with that name.');
  }
  const source = form.source === undefined ? script.source : String(form.source).replace(/\r\n/g, '\n');
  if (source.length > MAX_SOURCE) throw new UserError('A script can be up to 256 KB.');
  let mode = script.mode;
  let blocks = script.blocks;
  if (form.mode === 'code') { mode = 'code'; blocks = ''; }
  else if (script.mode === 'blocks' && form.blocks !== undefined) {
    blocks = String(form.blocks);
    if (blocks.length > MAX_BLOCKS) throw new UserError('That block script is too big.');
    try { JSON.parse(blocks || '{}'); } catch { throw new UserError('The blocks could not be read. Reload the page and try again.'); }
  }
  await db.getPool().query(
    `UPDATE ${table(server, 'lua_scripts')} SET name = ?, mode = ?, source = ?, blocks = ?, updated_by = ? WHERE id = ?`,
    [name, mode, source, blocks, String(actor).slice(0, 24), script.id]);
  return tellGame(server);
}

async function setEnabled(server, script, enabled, actor) {
  await db.getPool().query(`UPDATE ${table(server, 'lua_scripts')} SET enabled = ?, updated_by = ? WHERE id = ?`,
    [enabled ? 1 : 0, String(actor).slice(0, 24), script.id]);
  return tellGame(server);
}

async function remove(server, script) {
  await db.getPool().query(`DELETE FROM ${table(server, 'lua_scripts')} WHERE id = ?`, [script.id]);
  return tellGame(server);
}

async function duplicate(server, script, actor) {
  const scripts = await list(server);
  let name = `${script.name} copy`.slice(0, 48);
  for (let n = 2; scripts.some((s) => s.name.toLowerCase() === name.toLowerCase()); n++) name = `${script.name.slice(0, 40)} copy ${n}`;
  const result = await create(server, { name, mode: script.mode, source: script.source, blocks: script.blocks }, actor);
  await db.getPool().query(`UPDATE ${table(server, 'lua_scripts')} SET enabled = 0 WHERE id = ?`, [result.id]); // @note two copies running would do everything twice
  await tellGame(server);
  return result;
}

/* @return log rows after `after` (newest last), at most `limit` */
async function logs(server, { after = 0, limit = 200, script = '' } = {}) {
  try {
    await ensure(server);
    const params = [Number(after) || 0];
    let where = 'id > ?';
    if (script) { where += ' AND (script = ? OR script = \'(server)\')'; params.push(script); }
    params.push(Math.min(500, Math.max(1, Number(limit) || 200)));
    const [rows] = await db.getPool().query(
      `SELECT id, at, script, level, message FROM ${table(server, 'lua_log')} WHERE ${where} ORDER BY id DESC LIMIT ?`, params);
    return rows.reverse().map((r) => ({ id: Number(r.id), at: r.at, script: r.script, level: r.level, message: r.message }));
  } catch (err) {
    if (missingTable(err)) return [];
    throw err;
  }
}

async function clearLogs(server) {
  await ensure(server);
  await db.getPool().query(`DELETE FROM ${table(server, 'lua_log')}`);
}

module.exports = {
  UserError, MAX_SCRIPTS, MAX_SOURCE,
  list, get, create, update, setEnabled, remove, duplicate, logs, clearLogs,
};
