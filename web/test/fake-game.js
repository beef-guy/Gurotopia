#!/usr/bin/env node
'use strict';
// Stands in for main.out in tests: same flags, reads mysql_login.txt from its folder,
// reports status like the real server, saves on SIGINT. A name starting with "Crash" exits at once.
const fs = require('node:fs');
const mysql = require('mysql2/promise');

const args = {};
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (a === '--no-https') args.noHttps = true;
  else args[a.slice(2)] = process.argv[++i];
}
const login = Object.fromEntries(fs.readFileSync('mysql_login.txt', 'utf8').split('\n').filter(Boolean).map((l) => l.split('|')));

(async () => {
  console.log(`fake server ${args.name} on port ${args.port}, database ${args.database}, max ${args['max-players']}`);
  if (!fs.existsSync('items.dat')) console.error('items.dat missing');
  if (args.name.startsWith('Crash')) { console.error('boom'); process.exit(3); }
  const conn = await mysql.createConnection({ host: login.host, user: login.user, password: login.password });
  await conn.query(`CREATE DATABASE IF NOT EXISTS \`${args.database}\``);
  await conn.query(`USE \`${args['platform-db']}\``);
  await conn.query("UPDATE servers SET status='running', online=2, last_seen=UTC_TIMESTAMP() WHERE name=?", [args.name]);
  // @note stands in for include/tools/panel_bridge.cpp: carries out the website's actions and says who is online
  const own = await mysql.createConnection({ host: login.host, user: login.user, password: login.password, database: args.database });
  await own.query(`CREATE TABLE IF NOT EXISTS panel_actions (id INT AUTO_INCREMENT PRIMARY KEY, action VARCHAR(16) NOT NULL,
    target VARCHAR(24) NOT NULL, value VARCHAR(255) DEFAULT '', created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, done_at DATETIME NULL)`);
  await own.query("CREATE TABLE IF NOT EXISTS panel_live (growid VARCHAR(18) NOT NULL PRIMARY KEY, world VARCHAR(24) DEFAULT '')");
  await own.query("REPLACE INTO panel_live VALUES ('Griefer', 'TRADE'), ('Noob', '')");
  // @note what include/database/roles.cpp makes on start
  await own.query(`CREATE TABLE IF NOT EXISTS roles (id INT NOT NULL PRIMARY KEY, name VARCHAR(24) NOT NULL, priority INT NOT NULL DEFAULT 0,
    color VARCHAR(1) NOT NULL DEFAULT '', tag VARCHAR(4) NOT NULL DEFAULT '', powers INT NOT NULL DEFAULT 0, commands TEXT)`);
  await own.query(`INSERT IGNORE INTO roles VALUES (0, 'Player', 0, '', '', 0, '/me,/msg,/sb,/who'),
    (1, 'Moderator', 500, '#', '@', 1, '/ban,/kick,/me,/msg,/mute,/sb,/who'), (2, 'Developer', 1000, '6', '@', 2, '/ban,/give,/kick,/me,/msg,/mute,/sb,/who')`);
  await own.query('CREATE TABLE IF NOT EXISTS panel_commands (name VARCHAR(32) NOT NULL PRIMARY KEY, min_role INT NOT NULL DEFAULT 0)');
  await own.query("REPLACE INTO panel_commands VALUES ('/me', 0), ('/msg', 0), ('/sb', 0), ('/who', 0), ('/kick', 1), ('/ban', 1), ('/mute', 1), ('/give', 2), ('/newthing', 0)");
  setInterval(async () => {
    try {
      const [rows] = await own.query('SELECT id, action, target, value FROM panel_actions WHERE done_at IS NULL ORDER BY id');
      for (const r of rows) {
        if (r.action === 'world_delete') await own.query('DELETE FROM world WHERE name = ?', [r.target]);
        if (r.action === 'world_empty') await own.query('UPDATE panel_live SET world = \'\' WHERE world = ?', [r.target]);
        if (r.action === 'kick') await own.query('DELETE FROM panel_live WHERE growid = ?', [r.target]);
        await own.query('UPDATE panel_actions SET done_at = NOW() WHERE id = ?', [r.id]);
        console.log(`panel action ${r.action} ${r.target}${r.value ? ` ${r.value}` : ''}`);
      }
    } catch (err) { console.error(`panel bridge: ${err.message}`); }
  }, 200);
  console.log('ready');
  process.on('SIGINT', async () => {
    console.log('saving everything');
    await conn.query("UPDATE servers SET status='stopped', online=0 WHERE name=?", [args.name]);
    await conn.end();
    await own.query('DELETE FROM panel_live').catch(() => {});
    process.exit(0);
  });
  setInterval(() => {}, 1000);
})();
