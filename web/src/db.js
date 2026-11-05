'use strict';
const mysql = require('mysql2/promise');
const { config } = require('./config');

/*
 * The platform database is shared with the game.
 * - servers: the game (gateway and instances) reads it to route players and writes
 *   status, online and last_seen. The website inserts and deletes rows.
 *   Keep this definition identical to the one in the C++ code.
 * - panel_users, panel_sessions, server_control: only the website uses them.
 */
const SCHEMA = [
  `CREATE TABLE IF NOT EXISTS servers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(24) NOT NULL UNIQUE,
    owner_id INT,
    port INT NOT NULL UNIQUE,
    db_name VARCHAR(64) NOT NULL,
    max_players INT DEFAULT 50,
    status VARCHAR(16) DEFAULT 'stopped',
    online INT DEFAULT 0,
    last_seen TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
  )`,
  `CREATE TABLE IF NOT EXISTS panel_users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(20) NOT NULL UNIQUE,
    password_hash VARCHAR(100) NOT NULL,
    is_admin TINYINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
  )`,
  `CREATE TABLE IF NOT EXISTS panel_sessions (
    token_hash CHAR(64) PRIMARY KEY,
    user_id INT NOT NULL,
    csrf CHAR(43) NOT NULL,
    expires_at DATETIME NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX (user_id),
    INDEX (expires_at)
  )`,
  `CREATE TABLE IF NOT EXISTS server_control (
    server_id INT PRIMARY KEY,
    want_running TINYINT NOT NULL DEFAULT 0,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
  )`,
];

let pool = null;

function quoteIdent(name) {
  if (!/^[A-Za-z0-9_]{1,64}$/.test(name)) throw new Error(`refusing unsafe database name ${JSON.stringify(name)}`);
  return `\`${name}\``;
}

async function connect() {
  if (pool) return pool;
  const { host, port, user, password, platformDb } = config.db;

  const bootstrap = await mysql.createConnection({ host, port, user, password });
  try {
    await bootstrap.query(`CREATE DATABASE IF NOT EXISTS ${quoteIdent(platformDb)}`);
  } finally {
    await bootstrap.end();
  }

  pool = mysql.createPool({
    host, port, user, password,
    database: platformDb,
    connectionLimit: 10,
    timezone: 'Z',
    dateStrings: false,
  });
  for (const statement of SCHEMA) await pool.query(statement);
  return pool;
}

function getPool() {
  if (!pool) throw new Error('database not connected yet');
  return pool;
}

async function close() {
  if (pool) await pool.end();
  pool = null;
}

module.exports = { connect, getPool, close, quoteIdent };
