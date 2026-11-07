'use strict';
/*
 * Read and moderate a server's players. Each server keeps its accounts in its own
 * database (srv_<id>), in the peer table the game creates on its first start.
 *
 * Changes go straight into the database (the game never saves role, bans or mutes
 * over it), then a running server is told to re-read the player right away, so an
 * online player is kicked, muted or gets the new role within a second.
 */
const db = require('./db');
const live = require('./live');

const ROLES = ['Player', 'Moderator', 'Developer']; // @note enum role in include/database/peer.hpp
const SORTS = {
  newest: 'uid DESC',
  oldest: 'uid ASC',
  name: 'growid ASC',
  level: 'level DESC, xp DESC',
  gems: 'gems DESC',
  playtime: 'playtime DESC',
};
const PAGE_SIZE = 25;
const DURATIONS = { // @note what the ban and mute forms offer, in seconds. -1 is forever, like the game's /ban
  '1h': 3600, '1d': 86400, '3d': 3 * 86400, '7d': 7 * 86400, '30d': 30 * 86400, forever: -1,
};

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

/* @note a server that never started has no tables yet */
function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

function shape(row, now = Math.floor(Date.now() / 1000)) {
  const bannedUntil = Number(row.banned_until || 0);
  const mutedUntil = Number(row.muted_until || 0);
  const role = Number(row.role || 0);
  return {
    uid: row.uid,
    growid: row.growid,
    role,
    roleName: ROLES[role] || `Role ${role}`,
    level: Number(row.level || 1),
    xp: Number(row.xp || 0),
    gems: Number(row.gems || 0),
    wlStorage: Number(row.wl_storage || 0),
    piggyBank: Number(row.piggy_bank || 0),
    playtime: Number(row.playtime || 0),
    createdAt: row.created_at,
    banned: bannedUntil < 0 || bannedUntil > now,
    bannedUntil,
    banReason: row.ban_reason || '',
    muted: mutedUntil < 0 || mutedUntil > now,
    mutedUntil,
  };
}

const COLUMNS = 'uid, growid, created_at, gems, level, xp, role, wl_storage, playtime, banned_until, ban_reason, muted_until, piggy_bank';

async function summary(server) {
  try {
    const now = Math.floor(Date.now() / 1000);
    const [[row]] = await db.getPool().query(
      `SELECT COUNT(*) AS total,
              COALESCE(SUM(role > 0), 0) AS staff,
              COALESCE(SUM(banned_until < 0 OR banned_until > ?), 0) AS banned,
              COALESCE(SUM(muted_until < 0 OR muted_until > ?), 0) AS muted,
              COALESCE(SUM(created_at > UTC_TIMESTAMP() - INTERVAL 7 DAY), 0) AS week
       FROM ${table(server, 'peer')}`, [now, now]);
    return { ready: true, total: Number(row.total), staff: Number(row.staff), banned: Number(row.banned), muted: Number(row.muted), week: Number(row.week) };
  } catch (err) {
    if (missingTable(err)) return { ready: false, total: 0, staff: 0, banned: 0, muted: 0, week: 0 };
    throw err;
  }
}

/* @note filter: all | staff | banned | muted */
async function list(server, { q = '', sort = 'newest', filter = 'all', page = 1 } = {}) {
  const where = [];
  const args = [];
  const now = Math.floor(Date.now() / 1000);
  const search = String(q).trim().slice(0, 18);
  if (search) { where.push('growid LIKE ?'); args.push(`%${search.replace(/[\\%_]/g, (c) => `\\${c}`)}%`); }
  if (filter === 'staff') where.push('role > 0');
  if (filter === 'banned') { where.push('(banned_until < 0 OR banned_until > ?)'); args.push(now); }
  if (filter === 'muted') { where.push('(muted_until < 0 OR muted_until > ?)'); args.push(now); }
  const whereSql = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const order = SORTS[sort] || SORTS.newest;
  const current = Math.max(1, Number.parseInt(page, 10) || 1);

  try {
    const pool = db.getPool();
    const [[{ n }]] = await pool.query(`SELECT COUNT(*) AS n FROM ${table(server, 'peer')} ${whereSql}`, args);
    const pages = Math.max(1, Math.ceil(Number(n) / PAGE_SIZE));
    const pageNo = Math.min(current, pages);
    const [rows] = await pool.query(
      `SELECT ${COLUMNS} FROM ${table(server, 'peer')} ${whereSql} ORDER BY ${order} LIMIT ? OFFSET ?`,
      [...args, PAGE_SIZE, (pageNo - 1) * PAGE_SIZE]);
    return { ready: true, players: rows.map((r) => shape(r, now)), total: Number(n), page: pageNo, pages };
  } catch (err) {
    if (missingTable(err)) return { ready: false, players: [], total: 0, page: 1, pages: 1 };
    throw err;
  }
}

async function get(server, uid) {
  try {
    const [[row]] = await db.getPool().query(`SELECT ${COLUMNS} FROM ${table(server, 'peer')} WHERE uid = ?`, [uid]);
    return row ? shape(row) : null;
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
}

/* @note the extras on a player's page. each part is optional, older servers may not have every table */
async function details(server, player) {
  const pool = db.getPool();
  const out = { worlds: [], friends: 0, log: [] };
  try {
    const [rows] = await pool.query(`SELECT name FROM ${table(server, 'world')} WHERE owner = ? ORDER BY name LIMIT 50`, [player.uid]);
    out.worlds = rows.map((r) => r.name);
  } catch (err) { if (!missingTable(err)) throw err; }
  try {
    const [[row]] = await pool.query(`SELECT COUNT(*) AS n FROM ${table(server, 'friends')} WHERE uid = ?`, [player.uid]);
    out.friends = Number(row.n);
  } catch (err) { if (!missingTable(err)) throw err; }
  try {
    const [rows] = await pool.query(
      `SELECT at, actor, action, detail FROM ${table(server, 'mod_log')} WHERE target = ? ORDER BY id DESC LIMIT 20`, [player.growid]);
    out.log = rows;
  } catch (err) { if (!missingTable(err)) throw err; }
  return out;
}

async function logAction(server, actor, action, target, detail) {
  try {
    await db.getPool().query(
      `INSERT INTO ${table(server, 'mod_log')} (actor, action, target, detail) VALUES (?, ?, ?, ?)`,
      [`web:${actor}`.slice(0, 18), action.slice(0, 16), target.slice(0, 18), detail.slice(0, 255)]);
  } catch (err) {
    if (!missingTable(err)) throw err; // @note no mod_log yet on very old servers, the change itself still counts
  }
}

function untilFrom(duration) {
  if (!Object.hasOwn(DURATIONS, duration)) throw new UserError('Pick how long it lasts.');
  const seconds = DURATIONS[duration];
  return seconds < 0 ? -1 : Math.floor(Date.now() / 1000) + seconds;
}

/*
 * @note every change runs as one statement on one uid, so nothing else in the account is touched.
 * @return { done, live } where live is false when a running server could not be told (an older game build)
 */
async function moderate(server, player, actor, action, form) {
  const done = await change(server, player, actor, action, form);
  let id;
  if (action === 'kick') id = await live.queue(server, 'kick', player.growid, String(form.reason || '').trim().slice(0, 100));
  else id = await live.queue(server, 'player_sync', player.growid);
  if (id) await live.waitDone(server, id, 3000);
  return { done, live: !server.reporting || Boolean(id) };
}

async function change(server, player, actor, action, form) {
  const pool = db.getPool();
  const peer = table(server, 'peer');
  switch (action) {
    case 'ban': {
      const until = untilFrom(form.duration);
      const reason = String(form.reason || '').trim().slice(0, 128);
      await pool.query(`UPDATE ${peer} SET banned_until = ?, ban_reason = ? WHERE uid = ?`, [until, reason, player.uid]);
      await logAction(server, actor, 'ban', player.growid, `${form.duration}${reason ? `: ${reason}` : ''}`);
      return 'banned';
    }
    case 'unban':
      await pool.query(`UPDATE ${peer} SET banned_until = 0, ban_reason = '' WHERE uid = ?`, [player.uid]);
      await logAction(server, actor, 'unban', player.growid, '');
      return 'unbanned';
    case 'mute': {
      const until = untilFrom(form.duration);
      await pool.query(`UPDATE ${peer} SET muted_until = ? WHERE uid = ?`, [until, player.uid]);
      await logAction(server, actor, 'mute', player.growid, form.duration);
      return 'muted';
    }
    case 'unmute':
      await pool.query(`UPDATE ${peer} SET muted_until = 0 WHERE uid = ?`, [player.uid]);
      await logAction(server, actor, 'unmute', player.growid, '');
      return 'unmuted';
    case 'role': {
      const role = Number(form.role);
      if (!Number.isInteger(role) || role < 0 || role >= ROLES.length) throw new UserError('Pick a role.');
      await pool.query(`UPDATE ${peer} SET role = ? WHERE uid = ?`, [role, player.uid]);
      await logAction(server, actor, 'role', player.growid, ROLES[role]);
      return 'roleSet';
    }
    case 'kick':
      if (!server.reporting) throw new UserError('The server is not running, so nobody is online to kick.');
      await logAction(server, actor, 'kick', player.growid, String(form.reason || '').trim().slice(0, 100));
      return 'kicked';
    default:
      throw new UserError('Unknown action.');
  }
}

module.exports = { ROLES, SORTS, DURATIONS, PAGE_SIZE, UserError, summary, list, get, details, moderate, shape };
