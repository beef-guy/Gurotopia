'use strict';
/*
 * Small admin commands for the VPS:
 *   npm run admin -- make-admin <username>
 *   npm run admin -- remove-admin <username>
 *   npm run admin -- reset-password <username> <new password>
 *   npm run admin -- users
 */
const db = require('./db');
const auth = require('./auth');

async function main() {
  const [command, username, password] = process.argv.slice(2);
  await db.connect();
  const pool = db.getPool();
  try {
    if (command === 'users') {
      const [rows] = await pool.query('SELECT id, username, is_admin, created_at FROM panel_users ORDER BY id');
      for (const r of rows) console.log(`${r.id}\t${r.username}${r.is_admin ? '\t(admin)' : ''}`);
      return;
    }
    if (!username) throw new Error('give a username');
    if (command === 'make-admin' || command === 'remove-admin') {
      const [result] = await pool.query('UPDATE panel_users SET is_admin = ? WHERE username = ?', [command === 'make-admin' ? 1 : 0, username]);
      if (result.affectedRows === 0) throw new Error(`no user called ${username}`);
      console.log(`${username} is ${command === 'make-admin' ? 'now' : 'no longer'} an admin`);
      return;
    }
    if (command === 'reset-password') {
      const problem = auth.passwordProblem(password);
      if (problem) throw new Error(problem);
      const [result] = await pool.query('UPDATE panel_users SET password_hash = ? WHERE username = ?', [await auth.hashPassword(password), username]);
      if (result.affectedRows === 0) throw new Error(`no user called ${username}`);
      const [[user]] = await pool.query('SELECT id FROM panel_users WHERE username = ?', [username]);
      await pool.query('DELETE FROM panel_sessions WHERE user_id = ?', [user.id]); // @note log them out everywhere
      console.log(`password changed for ${username}`);
      return;
    }
    throw new Error('commands: users, make-admin <name>, remove-admin <name>, reset-password <name> <password>');
  } finally {
    await db.close();
  }
}

main().catch((err) => {
  console.error(err.message);
  process.exit(1);
});
