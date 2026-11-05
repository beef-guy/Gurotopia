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
  console.log('ready');
  process.on('SIGINT', async () => {
    console.log('saving everything');
    await conn.query("UPDATE servers SET status='stopped', online=0 WHERE name=?", [args.name]);
    await conn.end();
    process.exit(0);
  });
  setInterval(() => {}, 1000);
})();
