'use strict';
/*
 * All settings come from environment variables (see .env.example).
 * The panel and the supervisor read the same file, so they always agree.
 */
const path = require('node:path');

function str(name, fallback) {
  const v = process.env[name];
  return v === undefined || v === '' ? fallback : v;
}

function int(name, fallback) {
  const v = process.env[name];
  if (v === undefined || v === '') return fallback;
  const n = Number.parseInt(v, 10);
  if (!Number.isInteger(n)) throw new Error(`${name} must be a whole number, not "${v}"`);
  return n;
}

function bool(name, fallback) {
  const v = process.env[name];
  if (v === undefined || v === '') return fallback;
  return ['1', 'true', 'yes', 'on'].includes(v.toLowerCase());
}

const config = {
  db: {
    host: str('DB_HOST', '127.0.0.1'),
    port: int('DB_PORT', 3306),
    user: str('DB_USER', 'gurotopia'),
    password: str('DB_PASSWORD', ''),
    platformDb: str('PLATFORM_DB', 'gurotopia_platform'),
  },

  panel: {
    host: str('PANEL_HOST', '127.0.0.1'), // @note nginx in front handles HTTPS
    port: int('PANEL_PORT', 3000),
    publicUrl: str('PANEL_PUBLIC_URL', 'http://localhost:3000'),
    cookieSecure: bool('COOKIE_SECURE', false), // @note set true once the site is on HTTPS
    signupCode: str('SIGNUP_CODE', ''), // @note when set, people need this code to sign up (closed beta)
    serversPerUser: int('SERVERS_PER_USER', 3),
    maxPlayersLimit: int('MAX_PLAYERS_LIMIT', 100),
    defaultMaxPlayers: int('DEFAULT_MAX_PLAYERS', 50),
    sessionDays: int('SESSION_DAYS', 14),
    gameAddress: str('GAME_ADDRESS', ''), // @note the shared IP or hostname players point the game at, shown on the dashboard
  },

  supervisor: {
    host: '127.0.0.1', // @note never exposed outside the machine
    port: int('SUPERVISOR_PORT', 3001),
    token: str('SUPERVISOR_TOKEN', ''),
    gameDir: path.resolve(str('GAME_DIR', '/opt/gurotopia')), // @note holds main.out, items.dat, resources/, mysql_login.txt
    gameBin: str('GAME_BIN', 'main.out'), // @note relative to GAME_DIR unless absolute
    serversDir: path.resolve(str('SERVERS_DIR', '/srv/gurotopia/servers')),
    lineBuffered: bool('GAME_LINE_BUFFERED', true), // @note run through stdbuf -oL so logs show up live
    stopTimeoutMs: int('STOP_TIMEOUT_SECONDS', 30) * 1000,
    logMaxBytes: int('LOG_MAX_BYTES', 2 * 1024 * 1024),
    maxCrashes: int('MAX_CRASHES', 5), // @note this many crashes within crashWindowMs gives up until someone presses Start
    crashWindowMs: int('CRASH_WINDOW_MINUTES', 10) * 60 * 1000,
  },

  ports: {
    first: int('FIRST_GAME_PORT', 17100), // @note 17091 is the gateway
    last: int('LAST_GAME_PORT', 17999),
  },
};

config.supervisor.gameBinPath = path.isAbsolute(config.supervisor.gameBin)
  ? config.supervisor.gameBin
  : path.join(config.supervisor.gameDir, config.supervisor.gameBin);

function requireToken() {
  if (config.supervisor.token.length < 32) {
    throw new Error('SUPERVISOR_TOKEN must be set to a random string of at least 32 characters (try: openssl rand -hex 32)');
  }
}

module.exports = { config, requireToken };
