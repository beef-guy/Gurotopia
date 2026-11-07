'use strict';
/*
 * Talks to a running game server through its own database (see include/tools/panel_bridge.cpp):
 * - panel_actions: rows the game carries out within a second (kick, player_sync, world_empty, world_delete)
 * - panel_live: who is online and which world they are in, rewritten by the game every 5 seconds
 */
const db = require('./db');

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR');
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/*
 * @return the action's id, or null when the server is not running or its game build is too old
 * to have the bridge (then the change still applies at the player's next login).
 */
async function queue(server, action, target, value = '') {
  if (!server.reporting) return null;
  try {
    const [result] = await db.getPool().query(
      `INSERT INTO ${table(server, 'panel_actions')} (action, target, value) VALUES (?, ?, ?)`,
      [action, String(target).slice(0, 24), String(value).slice(0, 255)]);
    return result.insertId;
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
}

/* @return true once the game has carried the action out, false if it took longer than waitMs */
async function waitDone(server, id, waitMs = 4000) {
  if (!id) return false;
  const end = Date.now() + waitMs;
  while (Date.now() < end) {
    const [[row]] = await db.getPool().query(`SELECT done_at FROM ${table(server, 'panel_actions')} WHERE id = ?`, [id]);
    if (!row || row.done_at) return true;
    await sleep(200);
  }
  return false;
}

/* @return Map of lower-case growid -> { growid, world } for everyone online, empty when the server is off */
async function online(server) {
  if (!server.reporting) return new Map();
  try {
    const [rows] = await db.getPool().query(`SELECT growid, world FROM ${table(server, 'panel_live')}`);
    return new Map(rows.map((r) => [r.growid.toLowerCase(), { growid: r.growid, world: r.world || '' }]));
  } catch (err) {
    if (missingTable(err)) return new Map();
    throw err;
  }
}

module.exports = { queue, waitDone, online };
