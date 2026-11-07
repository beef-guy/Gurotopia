'use strict';
/*
 * A server's roles, in the roles table its game creates on start (include/database/roles.cpp).
 * Ids 0 Player, 1 Moderator and 2 Developer always exist. The website can add more, rename
 * and recolour any of them, pick each role's commands and staff powers, and order them:
 * a role can only kick, ban or give roles to the roles below it.
 * Every change asks a running game to re-read the table, so it applies within a second.
 */
const db = require('./db');
const live = require('./live');

const PLAYER = 0;
const MODERATOR = 1;
const DEVELOPER = 2;
const MAX_ROLES = 50;

/* @note Growtopia colour codes a name can have. key is a CSS-safe name for the swatch class */
const COLORS = [
  { code: '', key: 'plain', label: 'Normal (world colour)' },
  { code: 'w', key: 'white', label: 'White' },
  { code: '6', key: 'gold', label: 'Gold' },
  { code: '#', key: 'pink', label: 'Pink' },
  { code: '4', key: 'red', label: 'Red' },
  { code: '8', key: 'orange', label: 'Orange' },
  { code: '9', key: 'yellow', label: 'Yellow' },
  { code: '2', key: 'green', label: 'Green' },
  { code: '^', key: 'lime', label: 'Lime' },
  { code: 'r', key: 'leaf', label: 'Leaf' },
  { code: 'c', key: 'cyan', label: 'Cyan' },
  { code: 'e', key: 'sky', label: 'Sky blue' },
  { code: '1', key: 'ice', label: 'Ice' },
  { code: '5', key: 'lilac', label: 'Lilac' },
  { code: 'p', key: 'blush', label: 'Blush' },
  { code: 's', key: 'grey', label: 'Grey' },
];
const COLOR_BY_CODE = new Map(COLORS.map((c) => [c.code, c]));

const POWERS = [
  { value: PLAYER, label: 'None', text: 'A normal player. World locks and machines stop them like everyone else.' },
  { value: MODERATOR, label: 'Moderator', text: 'Can enter any world, build and break through locks, open locked machines and edit lock settings. Skips the Super Broadcast cost and can\'t be IP banned.' },
  { value: DEVELOPER, label: 'Developer', text: 'Everything a moderator can do, plus the developer badge, Carnival control and no /renderworld wait.' },
];

/* @note what the website says about each command. a command the game knows but this list doesn't goes under Other */
const GROUPS = [
  { name: 'Chat and fun', commands: {
    '/msg': 'Send a private message', '/sb': 'Super Broadcast to the whole server', '/me': 'Say something as an action',
    '/who': 'See who is in the world', '/time': 'Show the server time', '/news': 'Open the news', '/weather': 'Change their own weather',
    '/skin': 'Change skin colour', '/warp': 'Warp to a world', '/carnival': 'Go to the Carnival', '/renderworld': 'Post a picture of the world to Discord',
  } },
  { name: 'Moderation', commands: {
    '/kick': 'Kick a player off the server', '/ban': 'Ban a player', '/unban': 'Unban a player', '/mute': 'Mute a player', '/unmute': 'Unmute a player',
    '/pull': 'Pull a player to them', '/warpto': 'Warp to a player', '/broadcast': 'Message everyone online', '/bc': 'Short for /broadcast', '/ghost': 'Fly through blocks, unseen',
  } },
  { name: 'Items and economy', risky: 'Can change the economy', commands: {
    '/give': 'Spawn any item', '/givegems': 'Give gems', '/find': 'Search and spawn items', '/trashall': 'Empty their own inventory',
    '/mail': 'Send mail to players', '/maillayout': 'Edit the mail layout', '/mailrewards': 'Edit mail rewards', '/punch': 'Change their punch effect',
    '/passpoints': 'Give themselves Grow Pass points',
  } },
  { name: 'Server tools', risky: 'For trusted staff only', commands: {
    '/setrole': 'Give roles below their own', '/granttitle': 'Give a player a title', '/removetitle': 'Take a title away', '/export': 'Save a world to a file', '/import': 'Load a world from a file',
    '/fossil': 'Spawn a fossil rock', '/spawnghost': 'Spawn ghosts', '/ready': 'Finish machines in the world', '/geiger': 'Show the radiation source',
    '/transformflag': 'Test a transform effect', '/transformscan': 'Try every transform effect',
  } },
];

/* @note what a server whose game is too old for editable roles has (and what a new one starts with) */
const BUILTIN = [
  { id: DEVELOPER, name: 'Developer', priority: 1000, color: '6', tag: '@', powers: DEVELOPER },
  { id: MODERATOR, name: 'Moderator', priority: 500, color: '#', tag: '@', powers: MODERATOR },
  { id: PLAYER, name: 'Player', priority: 0, color: '', tag: '', powers: PLAYER },
];

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

function shape(row) {
  const id = Number(row.id);
  const color = COLOR_BY_CODE.get(row.color || '') || COLORS[0];
  return {
    id,
    name: row.name,
    priority: Number(row.priority || 0),
    color: color.code,
    colorKey: color.key,
    tag: row.tag || '',
    powers: id === DEVELOPER ? DEVELOPER : id === PLAYER ? PLAYER : Number(row.powers || 0),
    commands: String(row.commands || '').split(',').map((c) => c.trim()).filter(Boolean),
    builtin: id <= DEVELOPER,
    members: Number(row.members || 0),
    allCommands: id === DEVELOPER,
  };
}

/* @return roles, highest priority first, and ready: false when the game hasn't made the table yet */
async function list(server) {
  try {
    const [rows] = await db.getPool().query(
      `SELECT r.*, (SELECT COUNT(*) FROM ${table(server, 'peer')} p WHERE p.role = r.id) AS members
       FROM ${table(server, 'roles')} r ORDER BY r.priority DESC, r.id ASC`);
    return { ready: true, roles: rows.map(shape) };
  } catch (err) {
    if (missingTable(err)) return { ready: false, roles: BUILTIN.map((r) => shape({ ...r, commands: '' })) };
    throw err;
  }
}

/* @return Map of role id -> role, for naming roles on the players pages */
async function byId(server) {
  return new Map((await list(server)).roles.map((r) => [r.id, r]));
}

async function get(server, id) {
  return (await list(server)).roles.find((r) => r.id === id) || null;
}

/* @return the commands roles can be given, in groups, from what this server's game build has */
async function catalog(server) {
  let names = null;
  try {
    const [rows] = await db.getPool().query(`SELECT name FROM ${table(server, 'panel_commands')} ORDER BY name`);
    names = new Set(rows.map((r) => r.name));
  } catch (err) {
    if (!missingTable(err)) throw err;
  }
  const known = new Set();
  const groups = GROUPS.map((g) => {
    const commands = Object.entries(g.commands)
      .filter(([name]) => !names || names.has(name))
      .map(([name, text]) => { known.add(name); return { name, text }; });
    return { name: g.name, risky: g.risky || '', commands };
  }).filter((g) => g.commands.length);
  const other = names ? [...names].filter((n) => !known.has(n)).map((name) => ({ name, text: '' })) : [];
  if (other.length) groups.push({ name: 'Other', risky: '', commands: other });
  return groups;
}

function commandNames(groups) {
  return new Set(groups.flatMap((g) => g.commands.map((c) => c.name)));
}

async function logAction(server, actor, action, target, detail = '') {
  try {
    await db.getPool().query(
      `INSERT INTO ${table(server, 'mod_log')} (actor, action, target, detail) VALUES (?, ?, ?, ?)`,
      [`web:${actor}`.slice(0, 18), action.slice(0, 16), String(target).slice(0, 18), String(detail).slice(0, 255)]);
  } catch (err) {
    if (!missingTable(err)) throw err;
  }
}

/* @note tells a running game to re-read the roles. it also refreshes everyone's name colour */
async function tellGame(server) {
  const id = await live.queue(server, 'roles_reload', '-');
  if (id) await live.waitDone(server, id, 3000);
  return !server.reporting || Boolean(id);
}

/* @note keeps Developer on top and Player at the bottom, and spaces out the roles between them */
async function renumber(conn, server) {
  const [rows] = await conn.query(
    `SELECT id FROM ${table(server, 'roles')} WHERE id NOT IN (?, ?) ORDER BY priority DESC, id ASC`, [PLAYER, DEVELOPER]);
  for (let i = 0; i < rows.length; i++) {
    await conn.query(`UPDATE ${table(server, 'roles')} SET priority = ? WHERE id = ?`, [(rows.length - i) * 10, rows[i].id]);
  }
  await conn.query(`UPDATE ${table(server, 'roles')} SET priority = 1000 WHERE id = ?`, [DEVELOPER]);
  await conn.query(`UPDATE ${table(server, 'roles')} SET priority = 0 WHERE id = ?`, [PLAYER]);
}

function readForm(form, groups, role) {
  const name = String(form.name || '').trim().replace(/\s+/g, ' ');
  if (!/^[A-Za-z0-9 _-]{1,24}$/.test(name)) throw new UserError('Give the role a name of up to 24 letters, numbers or spaces.');
  const color = String(form.color || '');
  if (!COLOR_BY_CODE.has(color)) throw new UserError('Pick a name colour.');
  const tag = String(form.tag || '').trim();
  if (!/^[^`|\\\s]{0,4}$/.test(tag)) throw new UserError('The tag can be up to 4 characters, without spaces, ` or |.');
  let powers = Number(form.powers);
  if (!POWERS.some((p) => p.value === powers)) throw new UserError('Pick the staff powers.');
  if (role && role.id === DEVELOPER) powers = DEVELOPER;
  if (role && role.id === PLAYER) powers = PLAYER;
  const allowed = commandNames(groups);
  const picked = [].concat(form.commands || []).map(String).filter((c) => allowed.has(c));
  return { name, color, tag, powers, commands: [...new Set(picked)].sort().join(',') };
}

async function nameTaken(server, name, exceptId) {
  const [[row]] = await db.getPool().query(
    `SELECT id FROM ${table(server, 'roles')} WHERE LOWER(name) = LOWER(?) AND id <> ?`, [name, exceptId ?? -1]);
  return Boolean(row);
}

/* @return { id, live } */
async function create(server, form, actor) {
  const groups = await catalog(server);
  const data = readForm(form, groups, null);
  if (await nameTaken(server, data.name)) throw new UserError('There is already a role with that name.');
  const conn = await db.getPool().getConnection();
  let id;
  try {
    await conn.beginTransaction();
    const [[{ n, top }]] = await conn.query(`SELECT COUNT(*) AS n, COALESCE(MAX(id), 2) AS top FROM ${table(server, 'roles')} FOR UPDATE`);
    if (Number(n) >= MAX_ROLES) throw new UserError(`A server can have up to ${MAX_ROLES} roles.`);
    id = Math.max(Number(top) + 1, 3);
    // @note new roles start just above Player, so they can't outrank anyone until they're moved up
    await conn.query(`INSERT INTO ${table(server, 'roles')} (id, name, priority, color, tag, powers, commands) VALUES (?, ?, 1, ?, ?, ?, ?)`,
      [id, data.name, data.color, data.tag, data.powers, data.commands]);
    await renumber(conn, server);
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'role_create', data.name);
  return { id, live: await tellGame(server) };
}

async function update(server, role, form, actor) {
  const groups = await catalog(server);
  const data = readForm(form, groups, role);
  if (await nameTaken(server, data.name, role.id)) throw new UserError('There is already a role with that name.');
  await db.getPool().query(
    `UPDATE ${table(server, 'roles')} SET name = ?, color = ?, tag = ?, powers = ?, commands = ? WHERE id = ?`,
    [data.name, data.color, data.tag, data.powers, role.id === DEVELOPER ? role.commands.join(',') : data.commands, role.id]);
  await logAction(server, actor, 'role_edit', data.name);
  return { live: await tellGame(server) };
}

/* @note dir: up | down. Developer and Player stay where they are */
async function move(server, role, dir, actor) {
  if (role.id === PLAYER || role.id === DEVELOPER) throw new UserError(`${role.name} always stays at the ${role.id === PLAYER ? 'bottom' : 'top'}.`);
  const conn = await db.getPool().getConnection();
  try {
    await conn.beginTransaction();
    await renumber(conn, server);
    const [rows] = await conn.query(
      `SELECT id, priority FROM ${table(server, 'roles')} WHERE id NOT IN (?, ?) ORDER BY priority DESC FOR UPDATE`, [PLAYER, DEVELOPER]);
    const at = rows.findIndex((r) => Number(r.id) === role.id);
    const other = rows[dir === 'up' ? at - 1 : at + 1];
    if (at >= 0 && other) {
      await conn.query(`UPDATE ${table(server, 'roles')} SET priority = ? WHERE id = ?`, [other.priority, role.id]);
      await conn.query(`UPDATE ${table(server, 'roles')} SET priority = ? WHERE id = ?`, [rows[at].priority, other.id]);
    }
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'role_move', role.name, dir);
  return { live: await tellGame(server) };
}

/* @note everyone who had the role becomes a Player */
async function remove(server, role, actor) {
  if (role.builtin) throw new UserError(`${role.name} is built in and can't be deleted. You can rename it instead.`);
  const conn = await db.getPool().getConnection();
  try {
    await conn.beginTransaction();
    await conn.query(`UPDATE ${table(server, 'peer')} SET role = ? WHERE role = ?`, [PLAYER, role.id]);
    await conn.query(`DELETE FROM ${table(server, 'roles')} WHERE id = ?`, [role.id]);
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'role_delete', role.name, `${role.members} moved to Player`);
  return { live: await tellGame(server) };
}

module.exports = {
  PLAYER, MODERATOR, DEVELOPER, COLORS, POWERS, UserError,
  list, byId, get, catalog, create, update, move, remove,
};
