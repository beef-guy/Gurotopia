'use strict';
const crypto = require('node:crypto');
const bcrypt = require('bcryptjs');
const { config } = require('./config');
const db = require('./db');

const COOKIE = 'gt_session';
const BCRYPT_COST = 12;

function sha256(text) {
  return crypto.createHash('sha256').update(text).digest('hex');
}

function randomToken() {
  return crypto.randomBytes(32).toString('base64url'); // @note 43 characters
}

function validUsername(name) {
  return typeof name === 'string' && /^[A-Za-z0-9_]{3,20}$/.test(name);
}

function passwordProblem(password) {
  if (typeof password !== 'string' || password.length < 8) return 'Use at least 8 characters for the password.';
  if (password.length > 72) return 'Use at most 72 characters for the password.'; // @note bcrypt ignores the rest
  return null;
}

async function hashPassword(password) {
  return bcrypt.hash(password, BCRYPT_COST);
}

/* @note compare against a dummy hash when the user doesn't exist, so timing doesn't reveal usernames */
const DUMMY_HASH = bcrypt.hashSync('not a real password', BCRYPT_COST);

async function checkLogin(username, password) {
  const [rows] = await db.getPool().query(
    'SELECT id, username, password_hash, is_admin FROM panel_users WHERE username = ?', [username]);
  const user = rows[0];
  const ok = await bcrypt.compare(String(password || ''), user ? user.password_hash : DUMMY_HASH);
  return ok && user ? { id: user.id, username: user.username, isAdmin: !!user.is_admin } : null;
}

async function createUser(username, password) {
  const hash = await hashPassword(password);
  const [result] = await db.getPool().query(
    'INSERT INTO panel_users (username, password_hash) VALUES (?, ?)', [username, hash]);
  return result.insertId;
}

async function createSession(res, userId) {
  const token = randomToken();
  const csrf = randomToken();
  const days = config.panel.sessionDays;
  await db.getPool().query(
    'INSERT INTO panel_sessions (token_hash, user_id, csrf, expires_at) VALUES (?, ?, ?, UTC_TIMESTAMP() + INTERVAL ? DAY)',
    [sha256(token), userId, csrf, days]);
  res.cookie(COOKIE, token, {
    httpOnly: true,
    sameSite: 'lax',
    secure: config.panel.cookieSecure,
    maxAge: days * 24 * 3600 * 1000,
    path: '/',
  });
}

async function destroySession(req, res) {
  const token = req.cookies[COOKIE];
  if (token) await db.getPool().query('DELETE FROM panel_sessions WHERE token_hash = ?', [sha256(token)]);
  res.clearCookie(COOKIE, { path: '/' });
}

function parseCookies(header) {
  const out = {};
  for (const part of (header || '').split(';')) {
    const i = part.indexOf('=');
    if (i < 0) continue;
    const key = part.slice(0, i).trim();
    try { out[key] = decodeURIComponent(part.slice(i + 1).trim()); } catch { /* @note ignore bad cookies */ }
  }
  return out;
}

/* @note sets req.user and req.csrf when signed in */
async function loadSession(req, res, next) {
  try {
    req.cookies = parseCookies(req.headers.cookie);
    req.user = null;
    const token = req.cookies[COOKIE];
    if (token && token.length === 43) {
      const [rows] = await db.getPool().query(
        `SELECT u.id, u.username, u.is_admin, s.csrf FROM panel_sessions s
         JOIN panel_users u ON u.id = s.user_id
         WHERE s.token_hash = ? AND s.expires_at > UTC_TIMESTAMP()`, [sha256(token)]);
      if (rows[0]) {
        req.user = { id: rows[0].id, username: rows[0].username, isAdmin: !!rows[0].is_admin };
        req.csrf = rows[0].csrf;
      }
    }
    res.locals.user = req.user;
    res.locals.csrf = req.csrf || '';
    next();
  } catch (err) {
    next(err);
  }
}

function requireUser(req, res, next) {
  if (!req.user) return res.redirect(`/login?next=${encodeURIComponent(req.originalUrl)}`);
  next();
}

function requireAdmin(req, res, next) {
  if (!req.user || !req.user.isAdmin) return res.status(404).render('error', { title: 'Not found', message: 'That page does not exist.' });
  next();
}

/* @note every form that changes something carries the session's csrf token */
function checkCsrf(req, res, next) {
  if (req.method !== 'POST') return next();
  const given = Buffer.from(String(req.body?._csrf || ''));
  const wanted = Buffer.from(req.csrf || '');
  if (!req.user || wanted.length === 0 || given.length !== wanted.length || !crypto.timingSafeEqual(given, wanted)) {
    return res.status(403).render('error', { title: 'Try again', message: 'That form was out of date. Go back, reload the page and try again.' });
  }
  next();
}

async function cleanupSessions() {
  await db.getPool().query('DELETE FROM panel_sessions WHERE expires_at < UTC_TIMESTAMP()');
}

/* @note simple in-memory limiter: fine for one panel process */
function rateLimiter({ max, windowMs }) {
  const hits = new Map();
  setInterval(() => {
    const now = Date.now();
    for (const [key, list] of hits) {
      const fresh = list.filter((t) => now - t < windowMs);
      if (fresh.length) hits.set(key, fresh); else hits.delete(key);
    }
  }, windowMs).unref();
  return {
    tooMany(key) {
      const now = Date.now();
      const list = (hits.get(key) || []).filter((t) => now - t < windowMs);
      hits.set(key, list);
      return list.length >= max;
    },
    hit(key) {
      const list = hits.get(key) || [];
      list.push(Date.now());
      hits.set(key, list);
    },
  };
}

module.exports = {
  validUsername, passwordProblem, hashPassword, checkLogin, createUser,
  createSession, destroySession, loadSession, requireUser, requireAdmin, checkCsrf,
  cleanupSessions, rateLimiter,
};
