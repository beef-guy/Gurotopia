'use strict';
const { config } = require('./config');
const db = require('./db');

/* @note what players type on the login page, so keep it simple */
const NAME_RE = /^[A-Za-z0-9]{3,24}$/;
const RESERVED = [
  'admin', 'administrator', 'gateway', 'gurotopia', 'growtopia', 'ubisoft', 'official',
  'support', 'staff', 'moderator', 'mod', 'system', 'server', 'login', 'test', 'root',
];
/* @note a starting point. extend this, or swap in a proper word filter, before opening sign-ups */
const BLOCKED_PARTS = ['nigg', 'fagg', 'retard', 'hitler', 'nazi', 'rape'];

function nameProblem(name) {
  if (typeof name !== 'string' || !NAME_RE.test(name)) return 'Use 3 to 24 letters and digits, with no spaces.';
  const lower = name.toLowerCase();
  if (RESERVED.includes(lower)) return 'That name is reserved. Pick another.';
  if (BLOCKED_PARTS.some((part) => lower.includes(part))) return 'That name is not allowed. Pick another.';
  return null;
}

function maxPlayersProblem(value) {
  const n = Number(value);
  if (!Number.isInteger(n) || n < 1 || n > config.panel.maxPlayersLimit) {
    return `Max players must be a whole number from 1 to ${config.panel.maxPlayersLimit}.`;
  }
  return null;
}

/* @note a server whose instance hasn't reported in for 2 minutes is treated as offline */
const FRESH_SECONDS = 120;

const SELECT = `
  SELECT s.id, s.name, s.owner_id, s.port, s.db_name, s.max_players, s.status, s.online,
         s.last_seen, s.created_at, u.username AS owner_name,
         (s.last_seen IS NOT NULL AND s.last_seen > UTC_TIMESTAMP() - INTERVAL ${FRESH_SECONDS} SECOND) AS fresh
  FROM servers s LEFT JOIN panel_users u ON u.id = s.owner_id`;

function shape(row) {
  return {
    id: row.id,
    name: row.name,
    ownerId: row.owner_id,
    ownerName: row.owner_name,
    port: row.port,
    dbName: row.db_name,
    maxPlayers: row.max_players,
    gameStatus: row.status,
    online: row.fresh ? row.online : 0,
    lastSeen: row.last_seen,
    createdAt: row.created_at,
    reporting: !!row.fresh,
  };
}

async function listForOwner(ownerId) {
  const [rows] = await db.getPool().query(`${SELECT} WHERE s.owner_id = ? ORDER BY s.created_at`, [ownerId]);
  return rows.map(shape);
}

async function listAll() {
  const [rows] = await db.getPool().query(`${SELECT} ORDER BY s.created_at`);
  return rows.map(shape);
}

async function get(id) {
  const [rows] = await db.getPool().query(`${SELECT} WHERE s.id = ?`, [id]);
  return rows[0] ? shape(rows[0]) : null;
}

async function countForOwner(ownerId) {
  const [[row]] = await db.getPool().query('SELECT COUNT(*) AS n FROM servers WHERE owner_id = ?', [ownerId]);
  return Number(row.n);
}

async function freePort(conn) {
  const [rows] = await conn.query(
    'SELECT port FROM servers WHERE port BETWEEN ? AND ? ORDER BY port', [config.ports.first, config.ports.last]);
  const used = new Set(rows.map((r) => r.port));
  for (let p = config.ports.first; p <= config.ports.last; p++) if (!used.has(p)) return p;
  return null;
}

class UserError extends Error {}

/* @note returns the new server. throws UserError with a message to show the owner */
async function create(ownerId, name, maxPlayers) {
  const problem = nameProblem(name) || maxPlayersProblem(maxPlayers);
  if (problem) throw new UserError(problem);

  for (let attempt = 0; attempt < 5; attempt++) {
    const conn = await db.getPool().getConnection();
    try {
      await conn.beginTransaction();
      // @note lock this owner's rows so two quick submits can't both pass the limit
      const [[{ n }]] = await conn.query('SELECT COUNT(*) AS n FROM servers WHERE owner_id = ? FOR UPDATE', [ownerId]);
      if (Number(n) >= config.panel.serversPerUser) {
        throw new UserError(`You can have up to ${config.panel.serversPerUser} servers. Delete one to make another.`);
      }
      const port = await freePort(conn);
      if (port === null) throw new UserError('There is no room for more servers right now. Try again later.');

      const [result] = await conn.query(
        "INSERT INTO servers (name, owner_id, port, db_name, max_players, status) VALUES (?, ?, ?, 'pending', ?, 'stopped')",
        [name, ownerId, port, Number(maxPlayers)]);
      const id = result.insertId;
      await conn.query('UPDATE servers SET db_name = ? WHERE id = ?', [`srv_${id}`, id]);
      await conn.query('INSERT INTO server_control (server_id, want_running) VALUES (?, 0)', [id]);
      await conn.commit();
      return get(id);
    } catch (err) {
      await conn.rollback().catch(() => {});
      if (err.code === 'ER_DUP_ENTRY') {
        if (/for key '([^']*\.)?name'/.test(err.message)) throw new UserError('That name is taken. Pick another.');
        continue; // @note someone took the same port at the same moment, try the next one
      }
      throw err;
    } finally {
      conn.release();
    }
  }
  throw new UserError('Could not pick a port. Try again.');
}

async function updateMaxPlayers(id, maxPlayers) {
  const problem = maxPlayersProblem(maxPlayers);
  if (problem) throw new UserError(problem);
  await db.getPool().query('UPDATE servers SET max_players = ? WHERE id = ?', [Number(maxPlayers), id]);
}

/* @note the supervisor has already stopped the process and removed its folder */
async function destroy(server) {
  const pool = db.getPool();
  if (/^srv_[0-9]+$/.test(server.dbName)) { // @note never drop anything the website didn't create
    await pool.query(`DROP DATABASE IF EXISTS ${db.quoteIdent(server.dbName)}`);
  }
  await pool.query('DELETE FROM server_control WHERE server_id = ?', [server.id]);
  await pool.query('DELETE FROM servers WHERE id = ?', [server.id]);
}

module.exports = {
  UserError, nameProblem, maxPlayersProblem,
  listForOwner, listAll, get, countForOwner, create, updateMaxPlayers, destroy,
};
