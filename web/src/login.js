'use strict';
/*
 * The page the game client opens to log in (the loginurl in the gateway's server_data.php).
 * It adds a "Server name" box, so players pick which server the gateway sends them to.
 *
 * It never checks passwords itself. It packs what the player typed into the token the
 * client hands to the gateway, and the gateway checks the password in that server's own
 * database (PBKDF2, rate limited). So this service needs no database and no secrets.
 *
 * Client flow:
 *   POST /player/login/dashboard          -> this page (the body is the client's data)
 *   POST /player/growid/login/validate    -> {"status":"success","token":...} or the page with an error
 *   POST /player/growid/checktoken        -> refreshes a saved token (auto-login)
 */
const fs = require('node:fs');
const path = require('node:path');
const express = require('express');

const HOST = process.env.LOGIN_HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.LOGIN_PORT || '3002', 10);
const BRAND = process.env.LOGIN_BRAND || 'PSNode';

const GROWID_RE = /^[A-Za-z0-9]{3,18}$/;
const PASSWORD_RE = /^[A-Za-z0-9@._!-]{4,32}$/; // @note no & or =, they would break the token
const SERVER_RE = /^[A-Za-z0-9]{3,24}$/;

const template = fs.readFileSync(path.join(__dirname, 'views', 'login.html'), 'utf8');

function b64(s) { return Buffer.from(s, 'utf8').toString('base64'); }
function unb64(s) { return Buffer.from(String(s || ''), 'base64').toString('utf8'); }

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

/* @note the client posts its data as one big form key: { "key|val\nkey2|val2...": "" } */
function rawClientData(body) {
  if (body && typeof body === 'object') {
    const keys = Object.keys(body);
    if (keys.length > 0) return keys[0];
  }
  return '';
}

function renderPage({ clientToken = '', error = '', growId = '', server = '', register = false } = {}) {
  return template
    .replaceAll('{{brand}}', escapeHtml(BRAND))
    .replaceAll('{{token}}', escapeHtml(clientToken))
    .replaceAll('{{growId}}', escapeHtml(growId))
    .replaceAll('{{server}}', escapeHtml(server))
    .replaceAll('{{mode}}', register ? 'register' : 'login')
    .replace('{{error}}', error ? `<p class="error" role="alert">${escapeHtml(error)}</p>` : '');
}

function makeToken({ clientToken, growId, password, server, register }) {
  // @note same layout as before, plus server=. action::protocol reads growId, password, reg and server.
  let t = `_token=${clientToken}&growId=${growId}&password=${password}&reg=${register ? 1 : 0}`;
  if (server) t += `&server=${server}`;
  return b64(t);
}

function success(res, token) {
  res.type('text/html').send(JSON.stringify({
    status: 'success',
    message: 'Account Validated.',
    token,
    url: '',
    accountType: 'growtopia',
    accountAge: 2,
  }));
}

function createApp() {
  const app = express();
  app.disable('x-powered-by');
  app.set('trust proxy', 'loopback');
  app.use(express.urlencoded({ extended: false, limit: '32kb' }));
  app.use(express.json({ limit: '32kb' }));

  app.get('/', (_req, res) => res.redirect(302, '/player/login/dashboard'));

  app.all('/player/login/dashboard', (req, res) => {
    res.type('text/html').send(renderPage({ clientToken: b64(rawClientData(req.body)) }));
  });

  app.all('/player/growid/login/validate', (req, res) => {
    const f = req.body || {};
    const clientToken = typeof f._token === 'string' ? f._token.replace(/[^A-Za-z0-9+/=]/g, '') : '';
    const growId = String(f.growId || '').trim();
    const password = String(f.password || '');
    const server = String(f.server || '').trim();
    const register = f.mode === 'register';

    const fail = (error) => res.type('text/html').send(renderPage({ clientToken, error, growId, server, register }));

    if (!SERVER_RE.test(server)) return fail('Type the name of the server you want to join (3 to 24 letters and digits).');
    if (!GROWID_RE.test(growId)) return fail('Your GrowID must be 3 to 18 letters and digits.');
    if (!PASSWORD_RE.test(password)) return fail('Passwords are 4 to 32 characters: letters, digits and @ . _ ! -');
    if (register && password !== String(f.password_confirmation || '')) return fail('The two passwords are different.');

    success(res, makeToken({ clientToken, growId, password, server, register }));
  });

  /* @note the client calls this to log in again with a saved token */
  const checktoken = (req, res) => {
    let { refreshToken, clientData } = req.body || {};
    if ((!refreshToken || !clientData) && req.body && Object.keys(req.body).length === 1) {
      const params = new URLSearchParams(Object.keys(req.body)[0]);
      refreshToken = refreshToken || params.get('refreshToken');
      clientData = clientData || params.get('clientData');
    }
    if (!refreshToken || !clientData) return res.json({ status: 'error', message: 'Missing refreshToken or clientData' });

    const params = new URLSearchParams(unb64(refreshToken));
    const growId = params.get('growId') || '';
    const password = params.get('password') || '';
    const server = params.get('server') || '';
    if (!GROWID_RE.test(growId) || !PASSWORD_RE.test(password) || (server && !SERVER_RE.test(server))) {
      return res.json({ status: 'error', message: 'Please log in again.' });
    }
    // @note a saved token never registers again
    success(res, makeToken({ clientToken: b64(String(clientData)), growId, password, server, register: false }));
  };
  // @note the client only reads the new token from validate/checktoken, so send it there (307 keeps the POST body)
  app.all('/player/growid/checktoken', (_req, res) => res.redirect(307, '/player/growid/validate/checktoken'));
  app.all('/player/growid/validate/checktoken', checktoken);

  app.use((_req, res) => res.status(404).type('text/plain').send('Not found'));
  return app;
}

if (require.main === module) {
  createApp().listen(PORT, HOST, () => console.log(`login page on http://${HOST}:${PORT}`));
}

module.exports = { createApp, makeToken, renderPage };
