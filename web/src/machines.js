'use strict';
/*
 * A server's machine capacities, in the machines table its game fills on start (include/database/machines.cpp).
 * One row per machine item: magplants (MAGPLANT 5000, Gaia's Beacon, Unstable Tesseract), vending machines and
 * the Techno-Organic Engine. The game adds every machine items.dat has with the real game's numbers.
 * - capacity: the most items it holds (vending machines: 0 = no limit)
 * - magplants also: what they take (anything, seeds only, no seeds) and building mode (the MAGPLANT 5000 Remote)
 * A custom machine is a new item copied from one of them (made with the item manager's create, so it needs a
 * restart to be in the game), plus its row here. Capacity changes apply right away (panel action machines_reload).
 */
const db = require('./db');
const live = require('./live');
const items = require('./items');

const PER_PAGE = 25;
const MAX_CAPACITY = 1000000000;
const MAX_MACHINES = 2000;

/* @note item types (enum type in include/database/items.hpp) this page covers */
const KINDS = {
  111: { key: 'magplant', label: 'Magplant' },
  62: { key: 'vending', label: 'Vending machine' },
  125: { key: 'engine', label: 'Auto machine' },
};
const KIND_TYPES = Object.keys(KINDS).map(Number);

const ACCEPTS = [
  { value: 0, label: 'Any item', text: 'Blocks, seeds and everything else that can be traded.' },
  { value: 1, label: 'Seeds only', text: 'Like Gaia\'s Beacon.' },
  { value: 2, label: 'No seeds', text: 'Like the Unstable Tesseract.' },
];

const SORTS = {
  id: 'm.item ASC',
  name: 'name ASC, m.item ASC',
  capacity: 'm.capacity = 0, m.capacity ASC, m.item ASC',
};

class UserError extends Error {}

function table(server, name) {
  return `${db.quoteIdent(server.dbName)}.${db.quoteIdent(name)}`;
}

function missingTable(err) {
  return err && (err.code === 'ER_NO_SUCH_TABLE' || err.code === 'ER_BAD_DB_ERROR' || err.code === 'ER_BAD_FIELD_ERROR');
}

/* @return the real game's numbers, the same as defaults() in include/database/machines.cpp */
function defaults(item, type) {
  if (type === 111) {
    if (item === 6946) return { capacity: 1500, accepts: 1, remote: 0 }; // @note Gaia's Beacon
    if (item === 6948) return { capacity: 1500, accepts: 2, remote: 0 }; // @note Unstable Tesseract
    return { capacity: 5000, accepts: 0, remote: 1 };
  }
  if (type === 62) return { capacity: 0, accepts: 0, remote: 0 };
  return { capacity: 5000, accepts: 0, remote: 0 };
}

function shape(row) {
  const type = Number(row.type || 0);
  const kind = KINDS[type] || { key: 'other', label: 'Not a machine' };
  const out = {
    item: Number(row.item),
    name: row.name || `Item #${row.item}`,
    known: Boolean(row.name),
    type,
    kind: kind.key,
    kindLabel: kind.label,
    capacity: Number(row.capacity || 0),
    accepts: Number(row.accepts || 0),
    remote: Number(row.remote) === 1,
    builtin: Number(row.builtin) === 1,
    note: row.note || '',
    baseId: row.base_id === null || row.base_id === undefined ? null : Number(row.base_id),
    pending: !row.in_game, // @note a custom machine the game hasn't added yet (needs a restart)
    updated: row.updated || null,
  };
  out.defaults = defaults(out.baseId ?? out.item, type); // @note a custom machine resets to what it was copied from
  return out;
}

/* @return "5,000", or "No limit" */
function capacityText(capacity) {
  return capacity > 0 ? Number(capacity).toLocaleString('en-US') : 'No limit';
}

const SELECT = (server) => `SELECT m.*, COALESCE(e.name, c.name, b.name) AS name, COALESCE(e.type, c.type, b.type) AS type, e.base_id, c.id IS NOT NULL AS in_game
  FROM ${table(server, 'machines')} m
  LEFT JOIN ${table(server, 'item_catalog')} c ON c.id = m.item
  LEFT JOIN ${table(server, 'item_edits')} e ON e.id = m.item AND e.removed = 0
  LEFT JOIN ${table(server, 'item_catalog')} b ON b.id = e.base_id`;

/* @return { ready, machines, total, page, pages } */
async function list(server, { q = '', sort = 'id', dir = 'asc', page = 1 } = {}) {
  try {
    const where = ['COALESCE(e.type, c.type, b.type) IN (?)'];
    const args = [KIND_TYPES];
    const text = String(q || '').trim().slice(0, 40);
    if (text) {
      const like = `%${text.replace(/[%_\\]/g, (m) => `\\${m}`)}%`;
      const id = text.replace(/^#/, '');
      if (/^[0-9]{1,5}$/.test(id)) { where.push('(m.item = ? OR COALESCE(e.name, c.name, b.name) LIKE ?)'); args.push(Number(id), like); }
      else { where.push('COALESCE(e.name, c.name, b.name) LIKE ?'); args.push(like); }
    }
    const filter = `WHERE ${where.join(' AND ')}`;
    const from = `FROM ${table(server, 'machines')} m
       LEFT JOIN ${table(server, 'item_catalog')} c ON c.id = m.item
       LEFT JOIN ${table(server, 'item_edits')} e ON e.id = m.item AND e.removed = 0
       LEFT JOIN ${table(server, 'item_catalog')} b ON b.id = e.base_id`;
    const [[{ n }]] = await db.getPool().query(`SELECT COUNT(*) AS n ${from} ${filter}`, args);
    const total = Number(n);
    const pages = Math.max(1, Math.ceil(total / PER_PAGE));
    const p = Math.min(Math.max(1, Number(page) || 1), pages);
    let order = SORTS[sort] || SORTS.id;
    if (dir === 'desc') order = order.split(', ').map((part) => (/ ASC$/.test(part) ? part.replace(/ ASC$/, ' DESC') : `${part} DESC`)).join(', ');
    const [rows] = await db.getPool().query(`${SELECT(server)} ${filter} ORDER BY ${order} LIMIT ? OFFSET ?`, [...args, PER_PAGE, (p - 1) * PER_PAGE]);
    return { ready: true, machines: rows.map(shape), total, page: p, pages };
  } catch (err) {
    if (missingTable(err)) return { ready: false, machines: [], total: 0, page: 1, pages: 1 };
    throw err;
  }
}

async function ready(server) {
  try {
    await db.getPool().query(`SELECT item FROM ${table(server, 'machines')} LIMIT 1`);
    return true;
  } catch (err) {
    if (missingTable(err)) return false;
    throw err;
  }
}

async function get(server, item) {
  if (!Number.isInteger(item) || item < 1 || item > 65535) return null;
  try {
    const [[row]] = await db.getPool().query(`${SELECT(server)} WHERE m.item = ?`, [item]);
    if (!row || !KINDS[Number(row.type)]) return null;
    return shape(row);
  } catch (err) {
    if (missingTable(err)) return null;
    throw err;
  }
}

/* @return the game's own machines, to copy a custom one from */
async function bases(server) {
  try {
    const [rows] = await db.getPool().query(`${SELECT(server)} WHERE m.builtin = 1 AND c.type IN (?) ORDER BY c.type = 111 DESC, m.item ASC`, [KIND_TYPES]);
    return rows.map(shape);
  } catch (err) {
    if (missingTable(err)) return [];
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
  const id = await live.queue(server, 'machines_reload', '-');
  if (id) await live.waitDone(server, id, 3000);
  return !server.reporting || Boolean(id);
}

const NOTE_RE = /^[^|\\\r\n]*$/;
const checked = (v) => v === '1' || v === 'on' || (Array.isArray(v) && v.includes('1'));

/* @return { capacity, accepts, remote, note } @param type the machine's item type */
function readSettings(form, type) {
  const raw = String(form.capacity ?? '').trim().replace(/[,\s_]/g, '');
  const vending = type === 62;
  const capacity = raw === '' && vending ? 0 : Number(raw);
  if (!/^[0-9]*$/.test(raw) || !Number.isInteger(capacity) || capacity < (vending ? 0 : 1) || capacity > MAX_CAPACITY) {
    throw new UserError(vending
      ? `Capacity is a whole number up to ${MAX_CAPACITY.toLocaleString('en-US')}, or 0 for no limit.`
      : `Capacity is a whole number from 1 to ${MAX_CAPACITY.toLocaleString('en-US')}.`);
  }
  const out = { capacity, accepts: 0, remote: 0 };
  if (type === 111) {
    const accepts = Number(form.accepts ?? 0);
    if (!ACCEPTS.some((a) => a.value === accepts)) throw new UserError('Pick what the magplant takes.');
    out.accepts = accepts;
    out.remote = checked(form.remote) ? 1 : 0;
  }
  const note = String(form.note || '').trim().replace(/\s+/g, ' ');
  if (note.length > 120 || !NOTE_RE.test(note)) throw new UserError('The note can be up to 120 characters, without |.');
  out.note = note;
  return out;
}

function summary(type, s) {
  const parts = [`capacity ${capacityText(s.capacity)}`];
  if (type === 111) parts.push(ACCEPTS.find((a) => a.value === s.accepts).label.toLowerCase(), s.remote ? 'building mode' : 'no building mode');
  return parts.join(', ');
}

/* @note a new machine: a custom item copied from one of the game's machines, with its own capacity. @return { item, live } */
async function create(server, form, actor) {
  const baseId = Number(String(form.base || '').replace('#', ''));
  const base = Number.isInteger(baseId) && baseId > 0 ? await get(server, baseId) : null;
  if (!base || !base.builtin || base.pending) throw new UserError('Pick the machine to copy.');
  const settings = readSettings(form, base.type);
  const [[{ n }]] = await db.getPool().query(`SELECT COUNT(*) AS n FROM ${table(server, 'machines')} WHERE builtin = 0`);
  if (Number(n) >= MAX_MACHINES) throw new UserError(`A server can have up to ${MAX_MACHINES} custom machines.`);
  let made;
  try {
    made = await items.create(server, { base: String(base.item), name: form.name }, actor);
  } catch (err) {
    if (err instanceof items.UserError) throw new UserError(err.message.replace('the new item', 'the machine'));
    throw err;
  }
  await db.getPool().query(
    `INSERT INTO ${table(server, 'machines')} (item, capacity, accepts, remote, builtin, note) VALUES (?, ?, ?, ?, 0, ?)
     ON DUPLICATE KEY UPDATE capacity = VALUES(capacity), accepts = VALUES(accepts), remote = VALUES(remote), builtin = 0, note = VALUES(note)`,
    [made.id, settings.capacity, settings.accepts, settings.remote, settings.note]);
  await logAction(server, actor, 'machine_create', `#${made.id}`, `${String(form.name || '').trim()} (copy of #${base.item}), ${summary(base.type, settings)}`);
  return { item: made.id, live: await tellGame(server) };
}

async function update(server, machine, form, actor) {
  const s = readSettings(form, machine.type);
  await db.getPool().query(`UPDATE ${table(server, 'machines')} SET capacity = ?, accepts = ?, remote = ?, note = ? WHERE item = ?`,
    [s.capacity, s.accepts, s.remote, s.note, machine.item]);
  await logAction(server, actor, 'machine_edit', `#${machine.item}`, summary(machine.type, s));
  return { live: await tellGame(server) };
}

/* @note a game machine goes back to the real game's numbers */
async function reset(server, machine, actor) {
  const d = machine.defaults;
  await db.getPool().query(`UPDATE ${table(server, 'machines')} SET capacity = ?, accepts = ?, remote = ? WHERE item = ?`, [d.capacity, d.accepts, d.remote, machine.item]);
  await logAction(server, actor, 'machine_reset', `#${machine.item}`, summary(machine.type, d));
  return { live: await tellGame(server) };
}

/* @note a custom machine is deleted with its item (and seed), which leaves the game on the next restart */
async function remove(server, machine, actor) {
  if (machine.builtin) throw new UserError('The game\'s own machines can\'t be deleted. Reset them instead.');
  const item = await items.get(server, machine.item);
  if (item && item.custom) await items.reset(server, item, actor);
  await db.getPool().query(`DELETE FROM ${table(server, 'machines')} WHERE item = ?`, [machine.item]);
  await logAction(server, actor, 'machine_delete', `#${machine.item}`, machine.name);
  return { live: await tellGame(server) };
}

module.exports = { KINDS, ACCEPTS, SORTS, MAX_CAPACITY, UserError, defaults, capacityText, list, ready, get, bases, create, update, reset, remove };
