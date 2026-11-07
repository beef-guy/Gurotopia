'use strict';
/*
 * A server's item effects, in the item_effects table its game creates on start (include/database/item_effects.cpp).
 * One row per item: buffs the item gives while it's worn or held, like +450% gems or one-hit breaking.
 * Everything a player wears adds up. Every change asks a running game to re-read, so it applies within a second.
 */
const db = require('./db');
const live = require('./live');
const items = require('./items');

const PER_PAGE = 25;
const MAX_EFFECTS = 2000;

/* @note column = the game's column. unit '%' or 'tiles', or a yes/no switch */
const EFFECTS = [
  { key: 'gems', column: 'gems', label: 'Extra gems', short: '+ Gems', unit: '%', max: 10000, text: 'More gems from breaking blocks. 100% is double.' },
  { key: 'xp', column: 'xp', label: 'Extra XP', short: '+ XP', unit: '%', max: 10000, text: 'More XP from everything: breaking, planting, harvesting.' },
  { key: 'oneHit', column: 'one_hit', label: 'One-hit breaking', short: 'One hit', unit: 'switch', text: 'Every block breaks in one punch. Magplants and auto machines still take their normal hits.' },
  { key: 'punchRange', column: 'punch_range', label: 'Extra break range', short: '+ Break range', unit: 'tiles', max: 10, text: 'Tiles of extra reach when punching.' },
  { key: 'buildRange', column: 'build_range', label: 'Extra build range', short: '+ Build range', unit: 'tiles', max: 10, text: 'Tiles of extra reach when placing blocks.' },
  { key: 'drops', column: 'drops', label: 'Extra drops', short: 'Extra drops', unit: '%', max: 1000, text: 'Chance of one more block or seed when breaking. Every 100% is one more for sure.' },
  { key: 'harvest', column: 'harvest', label: 'Extra harvest', short: '+ Harvest', unit: '%', max: 1000, text: 'More fruit from trees. 100% is double.' },
];

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

function shape(row) {
  const out = {
    item: Number(row.item),
    name: row.name || `Item #${row.item}`,
    known: Boolean(row.name),
    enabled: Number(row.enabled) === 1,
    note: row.note || '',
    updated: row.updated || null,
  };
  for (const e of EFFECTS) out[e.key] = e.unit === 'switch' ? Number(row[e.column]) === 1 : Number(row[e.column] || 0);
  out.count = EFFECTS.filter((e) => Boolean(out[e.key])).length;
  return out;
}

/* @return "+450%", "+2 tiles", "Yes", or "" when the effect is off */
function format(effect, value) {
  if (!value) return '';
  if (effect.unit === 'switch') return 'Yes';
  if (effect.unit === 'tiles') return `+${value}`;
  return `+${value}%`;
}

/* @return a short line for lists: "+450% gems, one-hit breaking" */
function summary(fx) {
  return EFFECTS.filter((e) => fx[e.key]).map((e) => (e.unit === 'switch' ? e.label.toLowerCase() : `${format(e, fx[e.key])} ${e.label.replace(/^Extra /, '').toLowerCase()}`)).join(', ');
}

const SELECT = (server) => `SELECT f.*, COALESCE(e.name, c.name) AS name
  FROM ${table(server, 'item_effects')} f
  LEFT JOIN ${table(server, 'item_catalog')} c ON c.id = f.item
  LEFT JOIN ${table(server, 'item_edits')} e ON e.id = f.item`;

/* @return { ready, effects, total, page, pages } */
async function list(server, { q = '', page = 1 } = {}) {
  try {
    const where = [];
    const args = [];
    const text = String(q || '').trim().slice(0, 40);
    if (text) {
      const id = text.replace(/^#/, '');
      if (/^[0-9]{1,5}$/.test(id)) { where.push('(f.item = ? OR COALESCE(e.name, c.name) LIKE ?)'); args.push(Number(id), `%${text}%`); }
      else { where.push('COALESCE(e.name, c.name) LIKE ?'); args.push(`%${text.replace(/[%_\\]/g, (m) => `\\${m}`)}%`); }
    }
    const filter = where.length ? `WHERE ${where.join(' AND ')}` : '';
    const [[{ n }]] = await db.getPool().query(
      `SELECT COUNT(*) AS n FROM ${table(server, 'item_effects')} f
       LEFT JOIN ${table(server, 'item_catalog')} c ON c.id = f.item LEFT JOIN ${table(server, 'item_edits')} e ON e.id = f.item ${filter}`, args);
    const total = Number(n);
    const pages = Math.max(1, Math.ceil(total / PER_PAGE));
    const p = Math.min(Math.max(1, Number(page) || 1), pages);
    const [rows] = await db.getPool().query(`${SELECT(server)} ${filter} ORDER BY f.updated DESC, f.item DESC LIMIT ? OFFSET ?`, [...args, PER_PAGE, (p - 1) * PER_PAGE]);
    return { ready: true, effects: rows.map(shape), total, page: p, pages };
  } catch (err) {
    if (missingTable(err)) return { ready: false, effects: [], total: 0, page: 1, pages: 1 };
    throw err;
  }
}

async function ready(server) {
  try {
    await db.getPool().query(`SELECT item FROM ${table(server, 'item_effects')} LIMIT 1`);
    return true;
  } catch (err) {
    if (missingTable(err)) return false;
    throw err;
  }
}

async function get(server, item) {
  if (!Number.isInteger(item) || item < 1 || item > 65535) return null;
  try {
    const [[row]] = await db.getPool().query(`${SELECT(server)} WHERE f.item = ?`, [item]);
    return row ? shape(row) : null;
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
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

async function tellGame(server) {
  const id = await live.queue(server, 'effects_reload', '-');
  if (id) await live.waitDone(server, id, 3000);
  return !server.reporting || Boolean(id);
}

const NOTE_RE = /^[^|\\\r\n]*$/;
const checked = (v) => v === '1' || v === 'on' || (Array.isArray(v) && v.includes('1'));

/* @return the values to save. @param fx the effect being edited, or null for a new one */
async function readForm(server, form, fx) {
  let item = fx ? fx.item : Number(form.item);
  if (!fx) {
    const found = Number.isInteger(item) && item > 0 ? await items.get(server, item) : null;
    if (!found) throw new UserError('Pick the item that gets the effects.');
    if (found.seed) throw new UserError('Seeds can\'t be worn. Pick the item itself.');
    item = found.id;
  }
  const data = { item };
  for (const e of EFFECTS) {
    if (e.unit === 'switch') { data[e.key] = checked(form[e.key]) ? 1 : 0; continue; }
    const raw = String(form[e.key] ?? '').trim();
    const v = raw === '' ? 0 : Number(raw);
    if (!Number.isInteger(v) || v < 0 || v > e.max) throw new UserError(`${e.label} is a whole number from 0 to ${e.max.toLocaleString('en-US')}.`);
    data[e.key] = v;
  }
  if (!EFFECTS.some((e) => data[e.key])) throw new UserError('Give the item at least one effect.');
  const note = String(form.note || '').trim().replace(/\s+/g, ' ');
  if (note.length > 120 || !NOTE_RE.test(note)) throw new UserError('The note can be up to 120 characters, without |.');
  data.note = note;
  data.enabled = checked(form.enabled) ? 1 : 0;
  return data;
}

const COLUMNS = [...EFFECTS.map((e) => e.column), 'note', 'enabled'];
const values = (d) => [...EFFECTS.map((e) => d[e.key]), d.note, d.enabled];

/* @return { item, live } */
async function create(server, form, actor) {
  const data = await readForm(server, form, null);
  if (await get(server, data.item)) throw new UserError('That item already has effects. Open it from the list to change them.');
  const [[{ n }]] = await db.getPool().query(`SELECT COUNT(*) AS n FROM ${table(server, 'item_effects')}`);
  if (Number(n) >= MAX_EFFECTS) throw new UserError(`A server can have effects on up to ${MAX_EFFECTS} items.`);
  await db.getPool().query(`INSERT INTO ${table(server, 'item_effects')} (item, ${COLUMNS.join(', ')}) VALUES (?, ${COLUMNS.map(() => '?').join(', ')})`,
    [data.item, ...values(data)]);
  await logAction(server, actor, 'effect_create', `#${data.item}`, summary(data));
  return { item: data.item, live: await tellGame(server) };
}

async function update(server, fx, form, actor) {
  const data = await readForm(server, form, fx);
  await db.getPool().query(`UPDATE ${table(server, 'item_effects')} SET ${COLUMNS.map((c) => `${c} = ?`).join(', ')} WHERE item = ?`, [...values(data), fx.item]);
  await logAction(server, actor, 'effect_edit', `#${fx.item}`, summary(data));
  return { live: await tellGame(server) };
}

async function remove(server, fx, actor) {
  await db.getPool().query(`DELETE FROM ${table(server, 'item_effects')} WHERE item = ?`, [fx.item]);
  await logAction(server, actor, 'effect_delete', `#${fx.item}`, fx.name);
  return { live: await tellGame(server) };
}

module.exports = { EFFECTS, UserError, format, summary, list, ready, get, create, update, remove };
