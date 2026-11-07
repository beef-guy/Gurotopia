'use strict';
/*
 * The item manager: every item in a server's items.dat, from the item_catalog table its game
 * writes on start, and the changes the website makes, in item_edits (include/database/items.cpp).
 * - an edit changes an item of items.dat (name, description, rarity, breaking, flags, texture)
 * - a custom item is a new item (and its seed) copied from an existing one, added after the game's own
 * The game rebuilds items.dat within a second of a change. Players see it after they log in again,
 * because the game client only loads items.dat at login.
 */
const db = require('./db');
const live = require('./live');

const PAGE_SIZE = 40;
const SORTS = { id: 'c.id ASC', newest: 'c.id DESC', name: 'c.name ASC, c.id ASC', rarity: 'c.rarity DESC, c.id ASC' };

/* @note enum type in include/database/items.hpp, the ones worth a name */
const TYPES = {
  0: 'Fist', 1: 'Wrench', 2: 'Door', 3: 'Lock', 4: 'Gems', 5: 'Treasure', 6: 'Deadly block', 7: 'Trampoline', 8: 'Consumable',
  9: 'Entrance', 10: 'Sign', 11: 'Sound block', 12: 'Animated toggle', 13: 'Main door', 14: 'Platform', 15: 'Bedrock', 16: 'Fire',
  17: 'Block', 18: 'Background', 19: 'Seed', 20: 'Clothing', 21: 'Animated block', 22: 'Sound background', 23: 'Art wall', 24: 'Bouncy',
  25: 'Spikes', 26: 'Portal', 27: 'Checkpoint', 28: 'Music note', 29: 'Ice', 31: 'Toggle block', 32: 'Chest', 33: 'Mailbox',
  34: 'Bulletin board', 35: 'Pinata', 36: 'Random block', 37: 'Component', 38: 'Provider', 39: 'Chemical combiner', 40: 'Achievement block',
  41: 'Weather machine', 42: 'Scoreboard', 43: 'Sungate', 46: 'Heart monitor', 47: 'Donation box', 49: 'Mannequin', 52: 'Game block',
  53: 'Game generator', 54: 'Xenonite', 55: 'Booth', 56: 'Crystal', 59: 'Spotlight', 61: 'Display block', 62: 'Vending machine',
  63: 'Fish tank port', 64: 'Fish', 65: 'Solar collector', 66: 'Forge', 69: 'Steam block', 75: 'Lobster trap', 82: 'Spirit storage',
  83: 'Display shelf', 84: 'VIP entrance', 85: 'Challenge timer', 90: 'Fossil', 97: 'Storage box', 98: 'Cooking oven', 99: 'Audio block',
  107: 'Aura', 110: 'Fishing block', 111: 'Magplant', 116: 'GrowScan', 130: 'Safe vault', 135: 'Ghost block',
};

/* @note what the filter tabs show */
const FILTERS = {
  all: { label: 'All items', where: '' }, // @note seeds are left out unless searched for, see list()
  custom: { label: 'Custom', where: 'c.custom = 1 AND c.name <> \'\' AND c.type <> 19' },
  edited: { label: 'Edited', where: 'e.id IS NOT NULL AND c.custom = 0' },
  clothing: { label: 'Clothing', where: 'c.type IN (20, 107)' },
  blocks: { label: 'Blocks', where: 'c.type IN (17, 18, 21, 22, 6, 7, 14, 15, 23, 24, 25, 29, 31)' },
  locks: { label: 'Locks', where: 'c.type = 3' },
  consumables: { label: 'Consumables', where: 'c.type = 8' },
  seeds: { label: 'Seeds', where: 'c.type = 19' },
};

const COLLISIONS = [
  { value: 0, label: 'Walk through it' },
  { value: 1, label: 'Solid' },
  { value: 2, label: 'Stand on top only (platform)' },
  { value: 3, label: 'Solid unless you have access (entrance)' },
  { value: 4, label: 'Toggles on and off' },
  { value: 7, label: 'Vertical' },
  { value: 12, label: 'Solid when stepped on' },
];

/* @note enum cat in items.hpp: the flags the editor shows. the others are kept as they are */
const FLAGS = [
  { bit: 0x80, key: 'untradeable', label: 'Untradeable', text: 'Can\'t be dropped or traded.' },
  { bit: 0x02, key: 'returns', label: 'Can\'t be destroyed', text: 'Smashing it puts it back in the backpack.' },
  { bit: 0x10, key: 'public', label: 'Public', text: 'Anyone can smash it, even in a locked world.' },
];

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

function typeName(type) {
  return TYPES[type] || `Type ${type}`;
}

/* @note a catalog row, with what the website changed on top (the catalog lags until the game has reloaded) */
function shape(row, edit = null) {
  const pick = (key, fallback) => (edit && edit[key] !== null && edit[key] !== undefined ? edit[key] : fallback);
  const cat = Number(pick('cat', row.cat || 0));
  const item = {
    id: Number(row.id),
    name: pick('name', row.name || ''),
    type: Number(row.type || 0),
    rarity: Number(pick('rarity', row.rarity || 0)),
    hits: Number(pick('hits', row.hits || 0)),
    hitReset: Number(pick('hit_reset', row.hit_reset || 0)),
    collision: Number(pick('collision', row.collision || 0)),
    cat,
    texture: pick('texture', row.texture || ''),
    texX: Number(pick('tex_x', row.tex_x || 0)),
    texY: Number(pick('tex_y', row.tex_y || 0)),
    info: pick('info', row.info || ''),
    custom: Boolean(Number(row.custom || 0)) || Boolean(edit && edit.base_id !== null && edit.base_id !== undefined),
    edited: Boolean(edit) || Boolean(Number(row.edited || 0)),
    baseId: edit && edit.base_id !== null && edit.base_id !== undefined ? Number(edit.base_id) : null,
  };
  item.typeName = typeName(item.type);
  item.seed = item.type === 19;
  for (const f of FLAGS) item[f.key] = Boolean(cat & f.bit);
  return item;
}

async function ready(server) {
  try {
    await db.getPool().query(`SELECT 1 FROM ${table(server, 'item_catalog')} LIMIT 1`);
    return true;
  } catch (err) {
    if (missingTable(err)) return false;
    throw err;
  }
}

async function summary(server) {
  try {
    const [[row]] = await db.getPool().query(
      `SELECT COALESCE(SUM(type <> 19), 0) AS total, COALESCE(SUM(type = 19), 0) AS seeds, COALESCE(SUM(custom = 1 AND name <> '' AND type <> 19), 0) AS custom, COALESCE(SUM(type IN (20, 107)), 0) AS clothing
       FROM ${table(server, 'item_catalog')}`);
    const [[edits]] = await db.getPool().query(
      `SELECT COUNT(*) AS edited FROM ${table(server, 'item_edits')} WHERE base_id IS NULL`);
    return { ready: true, total: Number(row.total), seeds: Number(row.seeds), custom: Number(row.custom), clothing: Number(row.clothing), edited: Number(edits.edited) };
  } catch (err) {
    if (missingTable(err)) return { ready: false, total: 0, seeds: 0, custom: 0, clothing: 0, edited: 0 };
    throw err;
  }
}

async function list(server, { q = '', sort = 'id', filter = 'all', page = 1 } = {}) {
  const where = ['c.name <> \'\' OR c.custom = 0'];
  const args = [];
  const search = String(q).trim().slice(0, 40);
  if (/^#?\d{1,5}$/.test(search)) { where.push('c.id = ?'); args.push(Number(search.replace('#', ''))); }
  else if (search) { where.push('c.name LIKE ?'); args.push(`%${search.replace(/[\\%_]/g, (c) => `\\${c}`)}%`); }
  if (FILTERS[filter] && FILTERS[filter].where) where.push(FILTERS[filter].where);
  if (!search && (filter === 'all' || !FILTERS[filter])) where.push('c.type <> 19'); // @note every block has a seed: they'd double the list
  const whereSql = `WHERE ${where.map((w) => `(${w})`).join(' AND ')}`;
  const order = SORTS[sort] || SORTS.id;
  const current = Math.max(1, Number.parseInt(page, 10) || 1);
  const from = `${table(server, 'item_catalog')} c LEFT JOIN ${table(server, 'item_edits')} e ON e.id = c.id`;
  try {
    const pool = db.getPool();
    const [[{ n }]] = await pool.query(`SELECT COUNT(*) AS n FROM ${from} ${whereSql}`, args);
    const pages = Math.max(1, Math.ceil(Number(n) / PAGE_SIZE));
    const pageNo = Math.min(current, pages);
    const [rows] = await pool.query(
      `SELECT c.*, e.id AS e_id, e.base_id, e.name AS e_name, e.rarity AS e_rarity, e.texture AS e_texture, e.tex_x AS e_tex_x, e.tex_y AS e_tex_y
       FROM ${from} ${whereSql} ORDER BY ${order} LIMIT ? OFFSET ?`, [...args, PAGE_SIZE, (pageNo - 1) * PAGE_SIZE]);
    const items = rows.map((r) => shape(r, r.e_id === null ? null : {
      base_id: r.base_id, name: r.e_name, rarity: r.e_rarity, texture: r.e_texture, tex_x: r.e_tex_x, tex_y: r.e_tex_y,
    }));
    return { ready: true, items, total: Number(n), page: pageNo, pages };
  } catch (err) {
    if (missingTable(err)) return { ready: false, items: [], total: 0, page: 1, pages: 1 };
    throw err;
  }
}

async function edits(server, ids) {
  if (!ids.length) return new Map();
  const [rows] = await db.getPool().query(`SELECT * FROM ${table(server, 'item_edits')} WHERE id IN (?)`, [ids]);
  return new Map(rows.map((r) => [Number(r.id), r]));
}

/* @return the item, or null. a custom item the game hasn't added yet is built from the item it copies */
async function get(server, id) {
  if (!Number.isInteger(id) || id < 0 || id > 65000) return null;
  try {
    const [[row]] = await db.getPool().query(`SELECT * FROM ${table(server, 'item_catalog')} WHERE id = ?`, [id]);
    const edit = (await edits(server, [id])).get(id) || null;
    if (edit && edit.removed) return null;
    if (row) return shape(row, edit);
    if (!edit || edit.base_id === null) return null;
    const [[base]] = await db.getPool().query(`SELECT * FROM ${table(server, 'item_catalog')} WHERE id = ?`, [edit.base_id]);
    if (!base) return null;
    const item = shape({ ...base, id, custom: 1 }, edit);
    item.pending = true;
    return item;
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
}

/* @return how many items items.dat itself has, so custom items go after them */
async function originalCount(server) {
  const [[row]] = await db.getPool().query(`SELECT COALESCE(MAX(id), -1) + 1 AS n FROM ${table(server, 'item_catalog')} WHERE custom = 0`);
  return Number(row.n);
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

/* @note asks a running game to rebuild items.dat. it takes a moment on a big items.dat */
async function tellGame(server) {
  const id = await live.queue(server, 'items_reload', '-');
  if (id) await live.waitDone(server, id, 8000);
  return !server.reporting || Boolean(id);
}

const TEXT_RE = /^[^`|\\\r\n]*$/;

function readForm(form, item) {
  const name = String(form.name ?? item.name).trim().replace(/\s+/g, ' ');
  if (name.length < 1 || name.length > 40 || !TEXT_RE.test(name)) throw new UserError('The name needs 1 to 40 characters, without `, | or \\.');
  const info = String(form.info ?? item.info).replace(/\s+/g, ' ').trim();
  if (info.length > 400 || !TEXT_RE.test(info)) throw new UserError('The description can be up to 400 characters, without `, | or \\.');
  const num = (key, min, max, label) => {
    const v = Number(form[key]);
    if (!Number.isInteger(v) || v < min || v > max) throw new UserError(`${label} must be a whole number from ${min} to ${max}.`);
    return v;
  };
  const rarity = num('rarity', 0, 999, 'Rarity');
  const hits = num('hits', 0, 42, 'Hits to break');
  const hitReset = num('hitReset', 0, 86400, 'Heal time');
  const collision = num('collision', 0, 12, 'Collision');
  if (!COLLISIONS.some((c) => c.value === collision) && collision !== item.collision) throw new UserError('Pick how players collide with it.');
  let cat = item.cat;
  for (const f of FLAGS) cat = form[f.key] ? (cat | f.bit) : (cat & ~f.bit);
  const texture = String(form.texture ?? item.texture).trim();
  if (!/^[A-Za-z0-9_./-]{1,64}$/.test(texture) || texture.includes('..') || !texture.endsWith('.rttex')) throw new UserError('The texture must be a .rttex file name from the game folder.');
  const texX = num('texX', 0, 255, 'Texture column');
  const texY = num('texY', 0, 255, 'Texture row');
  return { name, info, rarity, hits, hit_reset: hitReset, collision, cat, texture, tex_x: texX, tex_y: texY };
}

const EDIT_COLUMNS = ['name', 'info', 'rarity', 'hits', 'hit_reset', 'collision', 'cat', 'texture', 'tex_x', 'tex_y'];

async function update(server, item, form, actor) {
  const data = readForm(form, item);
  const cols = EDIT_COLUMNS.map((c) => db.quoteIdent(c));
  await db.getPool().query(
    `INSERT INTO ${table(server, 'item_edits')} (id, ${cols.join(', ')}) VALUES (?, ${cols.map(() => '?').join(', ')})
     ON DUPLICATE KEY UPDATE ${cols.map((c) => `${c} = VALUES(${c})`).join(', ')}, removed = 0`,
    [item.id, ...EDIT_COLUMNS.map((c) => data[c])]);
  await logAction(server, actor, 'item_edit', `#${item.id}`, data.name);
  return { live: await tellGame(server) };
}

/* @note a new item (and its seed, which every block has right after it) copied from an existing item */
async function create(server, form, actor) {
  let baseId = Number(String(form.base || '').replace('#', ''));
  if (!Number.isInteger(baseId) || baseId < 0) throw new UserError('Pick the item to start from.');
  const count = await originalCount(server);
  let base = await get(server, baseId);
  if (base && base.seed && baseId % 2 === 1) { baseId -= 1; base = await get(server, baseId); }
  if (!base || baseId >= count || baseId + 1 >= count) throw new UserError('Start from one of the game\'s own items (not a custom one).');
  const name = String(form.name || '').trim().replace(/\s+/g, ' ');
  if (name.length < 1 || name.length > 35 || !TEXT_RE.test(name)) throw new UserError('Give the new item a name of 1 to 35 characters, without `, | or \\.');
  const conn = await db.getPool().getConnection();
  let id;
  try {
    await conn.beginTransaction();
    const [[{ dup }]] = await conn.query(
      `SELECT (SELECT COUNT(*) FROM ${table(server, 'item_catalog')} WHERE LOWER(name) = LOWER(?))
            + (SELECT COUNT(*) FROM ${table(server, 'item_edits')} WHERE LOWER(name) = LOWER(?) AND removed = 0) AS dup`, [name, name]);
    if (Number(dup)) throw new UserError('An item with that name already exists.');
    const [[{ top }]] = await conn.query(`SELECT COALESCE(MAX(id), -1) AS top FROM ${table(server, 'item_edits')} WHERE base_id IS NOT NULL FOR UPDATE`);
    id = Math.max(count, Number(top) + 1);
    if (id % 2) id += 1; // @note blocks have even ids and their seed the odd one after
    if (id + 1 > 65000) throw new UserError('There is no room for more items.');
    await conn.query(`INSERT INTO ${table(server, 'item_edits')} (id, base_id, name) VALUES (?, ?, ?), (?, ?, ?)`,
      [id, baseId, name, id + 1, baseId + 1, `${name} Seed`]);
    await conn.commit();
  } catch (err) {
    await conn.rollback();
    throw err;
  } finally {
    conn.release();
  }
  await logAction(server, actor, 'item_create', `#${id}`, `${name} (copy of #${baseId})`);
  return { id, live: await tellGame(server) };
}

/* @note an edited item goes back to how items.dat has it. a custom item (and its seed) is removed */
async function reset(server, item, actor) {
  if (item.custom) {
    const block = item.id % 2 ? item.id - 1 : item.id;
    await db.getPool().query(`UPDATE ${table(server, 'item_edits')} SET removed = 1 WHERE id IN (?, ?)`, [block, block + 1]);
    await logAction(server, actor, 'item_delete', `#${block}`, item.name);
  } else {
    await db.getPool().query(`DELETE FROM ${table(server, 'item_edits')} WHERE id = ?`, [item.id]);
    await logAction(server, actor, 'item_reset', `#${item.id}`, item.name);
  }
  return { live: await tellGame(server) };
}

module.exports = {
  PAGE_SIZE, SORTS, FILTERS, COLLISIONS, FLAGS, TYPES, UserError,
  typeName, ready, summary, list, get, update, create, reset,
};
