'use strict';
/*
 * A server's titles, in the titles table its game creates on start (include/database/titles.cpp).
 * Ids 1-99 are the real game's titles (Doctor, Legendary, Max Level...): they can be edited and
 * switched off but not deleted. New titles get ids from 100 up.
 *
 * A title changes a player's name the way the real client draws titles:
 * - prefix and suffix text, and a name colour
 * - a badge: one of the words OnCountryState knows, some of which replace the flag
 * - an icon by the name: a 32x32 cell of a texture (an item's picture, an upload, or a game file)
 *
 * Who has a title is in peer_titles. Every change asks a running game to re-read, so it applies within a second.
 */
const db = require('./db');
const live = require('./live');
const roles = require('./roles');
const items = require('./items');

const MAX_TITLES = 100;
const FIRST_CUSTOM_ID = 100;

const BADGES = [
  { value: '', label: 'None' },
  { value: 'maxLevel', label: 'Max Level badge', text: 'The badge level 125 players get.' },
  { value: 'doctor', label: 'Doctor badge', text: 'What the Dr. title turns on.' },
  { value: 'donor', label: 'Grow4Good badge', text: 'Replaces the flag with the Grow4Good icon.' },
  { value: 'master', label: 'Mentor badge', text: 'Replaces the flag with the Mentor icon.' },
  { value: 'ccBadge', label: 'Content Creator badge', text: 'Replaces the flag with the Content Creator icon.' },
  { value: 'ttBadge', label: 'TikTok badge', text: 'Replaces the flag with the TikTok icon.' },
  { value: 'euphoriaTitle', label: 'Party Animal badge', text: 'The Euphoria event title.' },
];
const BADGE_BY_VALUE = new Map(BADGES.map((b) => [b.value, b]));

const GRANTS = [
  { value: 'manual', label: 'Only when given', text: 'Give it on a player\'s page, or in game with /granttitle.' },
  { value: 'everyone', label: 'Everyone', text: 'Every player has it and can switch it on.' },
  { value: 'level', label: 'From a level', text: 'Players earn it when they reach the level below.' },
  { value: 'role', label: 'With a role', text: 'Everyone with the role below has it.' },
];

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

/* @return the file name the panel's texture preview takes ("game/tiles_page16.rttex" -> "tiles_page16.rttex") */
function textureFile(texture) {
  return String(texture || '').replace(/^game\//, '');
}

function shape(row, colorByCode) {
  const id = Number(row.id);
  const color = colorByCode.get(row.color || '') || roles.COLORS[0];
  return {
    id,
    name: row.name,
    description: row.description || '',
    prefix: row.prefix || '',
    suffix: row.suffix || '',
    color: color.code,
    colorKey: color.key,
    badge: BADGE_BY_VALUE.has(row.badge || '') ? row.badge || '' : '',
    texture: row.texture || '',
    textureFile: textureFile(row.texture),
    texX: Number(row.tex_x || 0),
    texY: Number(row.tex_y || 0),
    iconItem: Number(row.icon_item || 0),
    grant: GRANTS.some((g) => g.value === row.grant_type) ? row.grant_type : 'manual',
    grantValue: Number(row.grant_value || 0),
    enabled: Number(row.enabled) === 1,
    sort: Number(row.sort || 0),
    builtin: Number(row.builtin) === 1 || id < FIRST_CUSTOM_ID,
    holders: Number(row.holders || 0),
  };
}

const colorMap = () => new Map(roles.COLORS.map((c) => [c.code, c]));

/* @return { ready, titles } in the order the game draws them */
async function list(server) {
  try {
    const [rows] = await db.getPool().query(
      `SELECT t.*, (SELECT COUNT(*) FROM ${table(server, 'peer_titles')} p WHERE p.title = t.id AND p.granted = 1) AS holders
       FROM ${table(server, 'titles')} t ORDER BY t.sort ASC, t.id ASC`);
    const colors = colorMap();
    return { ready: true, titles: rows.map((r) => shape(r, colors)) };
  } catch (err) {
    if (missingTable(err)) return { ready: false, titles: [] };
    throw err;
  }
}

async function get(server, id) {
  return (await list(server)).titles.find((t) => t.id === id) || null;
}

/* @return a short line on how players get it, for lists */
function howToGet(title, roleMap) {
  if (title.grant === 'everyone') return 'everyone';
  if (title.grant === 'level') return `from level ${title.grantValue}`;
  if (title.grant === 'role') { const r = roleMap && roleMap.get(title.grantValue); return `with the ${r ? r.name : 'deleted'} role`; }
  return 'given by staff';
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

async function tellGame(server, action = 'titles_reload', target = '-') {
  const id = await live.queue(server, action, target);
  if (id) await live.waitDone(server, id, 3000);
  return !server.reporting || Boolean(id);
}

const NAME_RE = /^[A-Za-z0-9 '!&.+-]{1,32}$/;
const DECOR_RE = /^[^|\\"\r\n]*$/; // @note colour codes (`4) are fine. | and new lines would break the game's packets
const TEX_RE = /^[A-Za-z0-9_.-]{1,80}\.rttex$/;

async function readForm(server, form, title) {
  const name = String(form.name || '').trim().replace(/\s+/g, ' ');
  if (!NAME_RE.test(name)) throw new UserError('Give the title a name of up to 32 letters, numbers or spaces.');
  const description = String(form.description || '').trim().replace(/\s+/g, ' ');
  if (description.length > 160 || !DECOR_RE.test(description)) throw new UserError('The description can be up to 160 characters, without | or quotes.');
  const prefix = String(form.prefix || '').replace(/^\s+/, '');
  const suffix = String(form.suffix || '').replace(/\s+$/, '');
  for (const [label, v] of [['before', prefix], ['after', suffix]]) {
    if (v.length > 24 || !DECOR_RE.test(v)) throw new UserError(`The text ${label} the name can be up to 24 characters, without | or quotes.`);
  }
  const color = String(form.color || '');
  if (!roles.COLORS.some((c) => c.code === color)) throw new UserError('Pick a name colour.');
  const badge = String(form.badge || '');
  if (!BADGE_BY_VALUE.has(badge)) throw new UserError('Pick a badge.');

  let texture = ''; let texX = 0; let texY = 0; let iconItem = 0;
  const icon = String(form.icon || 'none');
  if (icon === 'item') {
    iconItem = Number(form.iconItem);
    const item = Number.isInteger(iconItem) && iconItem > 0 ? await items.get(server, iconItem) : null;
    if (!item) throw new UserError('Pick the item whose picture the icon should use.');
    if (!item.texture || !TEX_RE.test(item.texture)) throw new UserError('That item has no picture the icon can use.');
    texture = `game/${item.texture}`; texX = item.texX; texY = item.texY;
  } else if (icon === 'texture') {
    const file = textureFile(String(form.texture || '').trim());
    if (!TEX_RE.test(file)) throw new UserError('Type a texture file that ends in .rttex, like tiles_page16.rttex, or pick one of your uploads.');
    texX = Number(form.texX); texY = Number(form.texY);
    if (!Number.isInteger(texX) || !Number.isInteger(texY) || texX < 0 || texY < 0 || texX > 63 || texY > 63) throw new UserError('The icon\'s column and row are numbers from 0 to 63.');
    texture = `game/${file}`;
  } else if (icon !== 'none') throw new UserError('Pick an icon.');

  const grant = String(form.grant || 'manual');
  if (!GRANTS.some((g) => g.value === grant)) throw new UserError('Pick how players get it.');
  let grantValue = 0;
  if (grant === 'level') {
    grantValue = Number(form.grantLevel);
    if (!Number.isInteger(grantValue) || grantValue < 1 || grantValue > 125) throw new UserError('The level is a number from 1 to 125.');
  } else if (grant === 'role') {
    grantValue = Number(form.grantRole);
    if (!(await roles.list(server)).roles.some((r) => r.id === grantValue)) throw new UserError('Pick the role that gets it.');
  }
  const enabled = form.enabled === '1' || form.enabled === 'on' || (Array.isArray(form.enabled) && form.enabled.includes('1'));
  let sort = Number(form.sort);
  if (!Number.isInteger(sort) || sort < 0 || sort > 999) sort = title ? title.sort : 100;
  return { name, description, prefix, suffix, color, badge, texture, texX, texY, iconItem, grant, grantValue, enabled: enabled ? 1 : 0, sort };
}

async function nameTaken(server, name, exceptId) {
  const [[row]] = await db.getPool().query(
    `SELECT id FROM ${table(server, 'titles')} WHERE LOWER(name) = LOWER(?) AND id <> ?`, [name, exceptId ?? -1]);
  return Boolean(row);
}

const COLUMNS = 'name = ?, description = ?, prefix = ?, suffix = ?, color = ?, badge = ?, texture = ?, tex_x = ?, tex_y = ?, icon_item = ?, grant_type = ?, grant_value = ?, enabled = ?, sort = ?';
const values = (d) => [d.name, d.description, d.prefix, d.suffix, d.color, d.badge, d.texture, d.texX, d.texY, d.iconItem, d.grant, d.grantValue, d.enabled, d.sort];

/* @return { id, live } */
async function create(server, form, actor) {
  const data = await readForm(server, form, null);
  if (await nameTaken(server, data.name)) throw new UserError('There is already a title with that name.');
  const conn = await db.getPool().getConnection();
  let id;
  try {
    await conn.beginTransaction();
    const [[{ n, top }]] = await conn.query(
      `SELECT COUNT(*) AS n, COALESCE(MAX(id), ?) AS top FROM ${table(server, 'titles')} WHERE id >= ? FOR UPDATE`, [FIRST_CUSTOM_ID - 1, FIRST_CUSTOM_ID]);
    if (Number(n) >= MAX_TITLES) throw new UserError(`A server can have up to ${MAX_TITLES} titles of its own.`);
    id = Math.max(Number(top) + 1, FIRST_CUSTOM_ID);
    await conn.query(`INSERT INTO ${table(server, 'titles')} SET id = ?, builtin = 0, ${COLUMNS}`, [id, ...values(data)]);
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'title_create', data.name);
  return { id, live: await tellGame(server) };
}

async function update(server, title, form, actor) {
  const data = await readForm(server, form, title);
  if (await nameTaken(server, data.name, title.id)) throw new UserError('There is already a title with that name.');
  await db.getPool().query(`UPDATE ${table(server, 'titles')} SET ${COLUMNS} WHERE id = ?`, [...values(data), title.id]);
  await logAction(server, actor, 'title_edit', data.name);
  return { live: await tellGame(server) };
}

async function remove(server, title, actor) {
  if (title.builtin) throw new UserError(`${title.name} is one of the real game's titles, so it can't be deleted. Switch it off instead.`);
  const conn = await db.getPool().getConnection();
  try {
    await conn.beginTransaction();
    await conn.query(`DELETE FROM ${table(server, 'peer_titles')} WHERE title = ?`, [title.id]);
    await conn.query(`DELETE FROM ${table(server, 'titles')} WHERE id = ?`, [title.id]);
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'title_delete', title.name, `${title.holders} had it`);
  return { live: await tellGame(server) };
}

/* @return every title with what this player has: { title, given, shown, owned, why } */
async function forPlayer(server, player) {
  const { ready, titles } = await list(server);
  if (!ready) return { ready, rows: [] };
  const [rows] = await db.getPool().query(`SELECT title, granted, equipped FROM ${table(server, 'peer_titles')} WHERE uid = ?`, [player.uid]);
  const mine = new Map(rows.map((r) => [Number(r.title), { granted: Number(r.granted) === 1, equipped: Number(r.equipped) === 1 }]));
  return {
    ready,
    rows: titles.map((t) => {
      const row = mine.get(t.id);
      const given = Boolean(row && row.granted);
      const byRule = t.grant === 'everyone' || (t.grant === 'level' && player.level >= t.grantValue) || (t.grant === 'role' && player.role === t.grantValue);
      const owned = t.enabled && (given || byRule);
      return { title: t, given, byRule, owned, shown: owned && (!row || row.equipped) };
    }),
  };
}

/* @note gives the titles ticked in the form and takes the rest. @return { live, changed } */
async function setForPlayer(server, player, form, actor) {
  const { ready, rows } = await forPlayer(server, player);
  if (!ready) throw new UserError('Start the server once with the latest game build before giving titles.');
  const want = new Set([].concat(form.titles || []).map(Number));
  const added = []; const removed = [];
  for (const r of rows) {
    const on = want.has(r.title.id);
    if (on === r.given) continue;
    if (on) {
      await db.getPool().query(
        `INSERT INTO ${table(server, 'peer_titles')} (uid, title, granted, equipped) VALUES (?, ?, 1, 1)
         ON DUPLICATE KEY UPDATE granted = 1, equipped = 1`, [player.uid, r.title.id]);
      added.push(r.title.name);
    } else {
      await db.getPool().query(`UPDATE ${table(server, 'peer_titles')} SET granted = 0 WHERE uid = ? AND title = ?`, [player.uid, r.title.id]);
      removed.push(r.title.name);
    }
  }
  if (!added.length && !removed.length) return { live: true, changed: false };
  await logAction(server, actor, 'titles', player.growid,
    [added.length ? `gave ${added.join(', ')}` : '', removed.length ? `took ${removed.join(', ')}` : ''].filter(Boolean).join('; '));
  return { live: await tellGame(server, 'titles_sync', player.growid), changed: true };
}

module.exports = {
  BADGES, GRANTS, FIRST_CUSTOM_ID, UserError, textureFile, howToGet,
  list, get, create, update, remove, forPlayer, setForPlayer,
};
