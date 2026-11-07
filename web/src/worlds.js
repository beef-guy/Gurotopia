'use strict';
/*
 * A server's worlds, from the world table in its own database. A world the game has
 * open is saved there every minute, so sizes and owners can lag a little behind.
 * Emptying or deleting a world while the server runs goes through the game (live.js),
 * because the game would otherwise save its open copy over the change.
 */
const db = require('./db');
const live = require('./live');

const PAGE_SIZE = 30;
const SORTS = {
  name: 'w.name ASC',
  size: 'size DESC',
  owner: 'owner_name ASC, w.name ASC',
  level: 'w.min_level DESC, w.name ASC',
};
const NAME_RE = /^[A-Za-z0-9]{1,24}$/;

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

function accessIds(text) {
  return String(text || '').split(',').map((v) => Number.parseInt(v, 10)).filter((n) => Number.isInteger(n) && n > 0);
}

function shape(row) {
  return {
    name: row.name,
    ownerId: Number(row.owner || 0),
    ownerName: row.owner_name || null,
    minLevel: Number(row.min_level || 1),
    access: accessIds(row.access),
    size: Number(row.size || 0),
  };
}

/* @note peer is joined for the owner's name. a world whose owner account is gone keeps its id */
function selectSql(server) {
  return `SELECT w.name, w.owner, w.min_level, w.access,
                 COALESCE(LENGTH(w.blocks), 0) + COALESCE(LENGTH(w.objects), 0) + COALESCE(LENGTH(w.extras), 0) AS size,
                 p.growid AS owner_name
          FROM ${table(server, 'world')} w LEFT JOIN ${table(server, 'peer')} p ON p.uid = w.owner`;
}

/* @return Map of upper-case world name -> [growid, ...] of who is inside right now */
async function visitors(server) {
  const byWorld = new Map();
  for (const { growid, world } of (await live.online(server)).values()) {
    if (!world) continue;
    const key = world.toUpperCase();
    if (!byWorld.has(key)) byWorld.set(key, []);
    byWorld.get(key).push(growid);
  }
  return byWorld;
}

async function list(server, { q = '', sort = 'name', filter = 'all', page = 1 } = {}) {
  const inside = await visitors(server);
  const where = [];
  const args = [];
  const search = String(q).trim().slice(0, 24);
  if (search) { where.push('w.name LIKE ?'); args.push(`%${search.replace(/[\\%_]/g, (c) => `\\${c}`)}%`); }
  if (filter === 'locked') where.push('w.owner > 0');
  if (filter === 'active') {
    if (inside.size === 0) where.push('FALSE');
    else { where.push(`UPPER(w.name) IN (${[...inside.keys()].map(() => '?').join(',')})`); args.push(...inside.keys()); }
  }
  const whereSql = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const order = SORTS[sort] || SORTS.name;
  const current = Math.max(1, Number.parseInt(page, 10) || 1);

  try {
    const pool = db.getPool();
    const [[{ n }]] = await pool.query(`SELECT COUNT(*) AS n FROM ${table(server, 'world')} w ${whereSql}`, args);
    const [[totals]] = await pool.query(
      `SELECT COUNT(*) AS total, COALESCE(SUM(owner > 0), 0) AS locked FROM ${table(server, 'world')}`);
    const pages = Math.max(1, Math.ceil(Number(n) / PAGE_SIZE));
    const pageNo = Math.min(current, pages);
    const [rows] = await pool.query(`${selectSql(server)} ${whereSql} ORDER BY ${order} LIMIT ? OFFSET ?`,
      [...args, PAGE_SIZE, (pageNo - 1) * PAGE_SIZE]);
    const worlds = rows.map((r) => ({ ...shape(r), inside: inside.get(r.name.toUpperCase()) || [] }));
    let playersInWorlds = 0;
    for (const names of inside.values()) playersInWorlds += names.length;
    return {
      ready: true, worlds, total: Number(n), page: pageNo, pages,
      stats: { total: Number(totals.total), locked: Number(totals.locked), active: inside.size, playersInWorlds },
    };
  } catch (err) {
    if (missingTable(err)) return { ready: false, worlds: [], total: 0, page: 1, pages: 1, stats: { total: 0, locked: 0, active: 0, playersInWorlds: 0 } };
    throw err;
  }
}

async function get(server, name) {
  if (!NAME_RE.test(name)) return null;
  try {
    const [[row]] = await db.getPool().query(`${selectSql(server)} WHERE w.name = ?`, [name]);
    if (!row) return null;
    const world = shape(row);
    world.inside = (await visitors(server)).get(world.name.toUpperCase()) || [];
    world.accessNames = [];
    if (world.access.length) {
      const [names] = await db.getPool().query(
        `SELECT uid, growid FROM ${table(server, 'peer')} WHERE uid IN (?)`, [world.access]);
      const byId = new Map(names.map((r) => [r.uid, r.growid]));
      world.accessNames = world.access.map((id) => ({ uid: id, growid: byId.get(id) || null }));
    }
    return world;
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
}

async function logAction(server, actor, action, target, detail = '') {
  try {
    await db.getPool().query(
      `INSERT INTO ${table(server, 'mod_log')} (actor, action, target, detail) VALUES (?, ?, ?, ?)`,
      [`web:${actor}`.slice(0, 18), action, target.slice(0, 18), detail.slice(0, 255)]);
  } catch (err) {
    if (!missingTable(err)) throw err;
  }
}

/* @return the done key for the message */
async function emptyWorld(server, world, actor) {
  if (!server.reporting) throw new UserError('The server is not running, so nobody is in this world.');
  const id = await live.queue(server, 'world_empty', world.name);
  if (!id) throw new UserError('This server runs an older game build. Restart it to update, then try again.');
  await live.waitDone(server, id);
  await logAction(server, actor, 'world_empty', world.name);
  return 'worldEmptied';
}

async function deleteWorld(server, world, actor, confirm) {
  if (String(confirm || '').toUpperCase() !== world.name.toUpperCase()) throw new UserError('Type the world name exactly to delete it.');
  if (server.reporting) {
    const id = await live.queue(server, 'world_delete', world.name);
    if (!id) throw new UserError('This server runs an older game build. Restart it to update, then try again.');
    if (!(await live.waitDone(server, id, 6000))) throw new UserError('The server did not answer in time. Check the console, then try again.');
  } else {
    await db.getPool().query(`DELETE FROM ${table(server, 'world')} WHERE name = ?`, [world.name]); // @note not running: nothing can save over it
  }
  await logAction(server, actor, 'world_delete', world.name);
  return 'worldDeleted';
}

module.exports = { SORTS, PAGE_SIZE, UserError, list, get, emptyWorld, deleteWorld };
