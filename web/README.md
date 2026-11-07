# Gurotopia server panel

The website where people create and run their own Gurotopia servers. An owner signs up, picks a server name and a player limit, and presses Start. Players join every server through the same address by typing the server name on the login screen. The gateway in the game code reads that name and sends them on.

It has two parts, each its own service:

- **Panel** (`src/panel.js`): the website. Sign up, log in, create, start, stop, restart, change max players, watch the live log, delete. Admins also get a page listing every server.
- **Supervisor** (`src/supervisor.js`): runs one `main.out` process per server. It restarts a server that crashes (and gives up after 5 crashes in 10 minutes), stops servers with SIGINT so they save, keeps a log per server, and starts the right servers again after a reboot. It only listens on 127.0.0.1 and needs a secret token, so only the panel can use it.

Because they are separate, updating or restarting the website never kicks players off a game server.

## Why this stack

Node.js with Express, plain server-rendered HTML (EJS templates), and MariaDB through `mysql2`. There are only 4 dependencies, no build step, and the pages are plain HTML you can edit directly. A React or Next.js front end would add a build pipeline without giving these pages anything they need yet. It can come later for the Lua script editor, which is the one page that needs a lot of browser code. The supervisor is in Node too, so there is one language and one `npm install` for the whole web side.

## How it fits with the game

```
players ──► nginx :443 ─┬─ panel.psnode.cloud ──► panel :3000 ──► supervisor :3001 ──► main.out per server
                        └─ the game's request ─► gateway :8443        (UDP 17100-17999)
players ──► gateway UDP 17091 ──(server name)──► that server's UDP port
                     all of them share one MariaDB: gurotopia_platform + srv_<id> per server
```

- The panel and the game share the `gurotopia_platform.servers` table. The panel inserts a row when a server is created (name, owner, port from 17100 up, database `srv_<id>`, max players) and deletes it when the server is deleted. The game writes `status`, `online` and `last_seen`. The panel shows a server as Online only when its process is up and it reported in within the last 2 minutes.
- The panel's own tables are `panel_users` (bcrypt password hashes), `panel_sessions` (only a hash of each session token is stored) and `server_control` (whether each server should be running, used after a reboot).
- Each server runs from its own folder, `/srv/gurotopia/servers/<id>/`, with `items.dat` and `resources/` linked from the shared install and its own copy of `mysql_login.txt`. Its log is in `logs/server.log` there, capped at 2 MB with one older file kept.
- The command line is exactly
  `main.out --name <name> --port <port> --database srv_<id> --platform-db gurotopia_platform --no-https --max-players <n>`.
- A new server is started straight after it is created, because its first start creates its `peer` and `world` tables.
- Deleting a server stops it, removes its folder and drops its `srv_<id>` database. The owner has to type the server name to confirm.

## The game's login page

`src/login.js` is the page the game client opens to log in. It has a **Server name** box, so players pick which server the gateway sends them to. It puts the server name, GrowID and password into the login token and never checks passwords itself: the gateway does that in the chosen server's database. So it needs no database and no secrets. It runs on 127.0.0.1:3002 as `gurotopia-login`, and nginx serves it as `login.psnode.cloud`. Set `loginurl|login.psnode.cloud` in the gateway's `/opt/gurotopia/server_data.php`. Try it locally with `npm run login`.

## Letting players connect (psnode.cloud/join)

The game asks `www.growtopia1.com` which server to join, over HTTPS, and only accepts a certificate Windows trusts. `deploy/join/make-game-cert.sh <public IP>` (run as root) makes a **PSNode Game CA** that is name-constrained to growtopia1.com and growtopia2.com, signs the game certificate with it, installs it in `/opt/gurotopia/resources/ctx/`, and writes `PSNode-Setup.cmd` and a short guide to `/var/www/psnode-join/`, which nginx serves at https://psnode.cloud/join/. Players run the setup once: it points the four growtopia hosts lines at the VPS and trusts that CA (choice 2 removes both). The CA key stays in `/root/psnode-ca`. Re-running the script keeps the CA and renews the game certificate (it lasts 825 days).

## Running it on your PC

You need Node.js 20 or newer and MariaDB.

```
cd web
npm install
copy .env.example .env      (cp on Linux)
```

Fill in `.env`: the MariaDB user and password, `SUPERVISOR_TOKEN` (any random 32+ characters), `GAME_DIR` (the folder with `main.out`, `items.dat`, `resources` and `mysql_login.txt`) and `SERVERS_DIR`. Then, in two terminals:

```
npm run supervisor
npm run panel
```

Open http://localhost:3000 and sign up. To make yourself an admin: `npm run admin -- make-admin <username>`.

The supervisor is written for the Linux VPS. On Windows it runs, but stopping a server there can't send the "save now" signal, so use it for trying the pages, not for real servers.

## Tests

```
npm test
```

The unit tests always run. The end-to-end test needs a local MariaDB user that can create databases:

```
TEST_DB_USER=root TEST_DB_PASSWORD=... npm test
```

It starts the real panel and supervisor, with `test/fake-game.js` standing in for `main.out`, and checks sign-up, login, the invite code, the CSRF check, that one owner can't see another's server, create, the name rules, start, stop (and that it saves), restart with a new player limit, crash retries, the per-owner server limit, servers coming back after the supervisor restarts, and delete. It uses its own `gurotopia_panel_test` database and drops it afterwards.

## Setting up the VPS (Ubuntu 24.04)

Run these as root, or with `sudo`. The files are set up for `panel.psnode.cloud`. First add a DNS A record for `panel` on psnode.cloud pointing at the VPS IP. To use another name, replace `panel.psnode.cloud` in `deploy/nginx.conf` and `panel.env`.

1. **Packages**
   ```
   apt update && apt install -y build-essential libssl-dev libmariadb-dev mariadb-server nginx certbot git
   curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && apt install -y nodejs
   ```
2. **A user for everything to run as**
   ```
   useradd --system --create-home --home-dir /opt/gurotopia --shell /usr/sbin/nologin gurotopia
   mkdir -p /srv/gurotopia/servers /etc/gurotopia && chown -R gurotopia:gurotopia /srv/gurotopia
   ```
3. **The game.** Build it and put it in `/opt/gurotopia`:
   ```
   git clone https://github.com/beef-guy/Gurotopia /opt/gurotopia-src
   cd /opt/gurotopia-src && make -j$(nproc)
   cp main.out /opt/gurotopia/ && cp -r resources /opt/gurotopia/
   ```
   Copy `items.dat` from your Growtopia folder into `/opt/gurotopia/`. Every server uses this one copy, so after a Growtopia update you replace it once and restart the servers.
4. **MariaDB.** Edit both passwords in `web/deploy/mariadb-setup.sql`, then run `mariadb < web/deploy/mariadb-setup.sql`. Create `/opt/gurotopia/mysql_login.txt` with the game user:
   ```
   host|127.0.0.1
   user|gurotopia_game
   password|<the game password>
   ```
   Then run `chown gurotopia:gurotopia /opt/gurotopia/mysql_login.txt && chmod 600 /opt/gurotopia/mysql_login.txt`.
5. **The website**
   ```
   cp -r /opt/gurotopia-src/web /opt/gurotopia-web
   cd /opt/gurotopia-web && npm ci --omit=dev
   cp .env.example /etc/gurotopia/panel.env
   ```
   Edit `/etc/gurotopia/panel.env`: set `DB_USER=gurotopia_panel`, its password, `SUPERVISOR_TOKEN` (from `openssl rand -hex 32`), `PANEL_PUBLIC_URL`, `GAME_ADDRESS`, and `SIGNUP_CODE` while it's invite-only. Then run `chmod 600 /etc/gurotopia/panel.env`.
6. **Services**
   ```
   cp /opt/gurotopia-web/deploy/*.service /etc/systemd/system/
   systemctl daemon-reload
   systemctl enable --now gurotopia-gateway gurotopia-supervisor gurotopia-panel gurotopia-login
   ```
7. **HTTPS.** Point `panel.psnode.cloud` at the VPS, run `certbot certonly --webroot -w /var/www/html -d panel.psnode.cloud` and `certbot certonly --webroot -w /var/www/html -d psnode.cloud -d www.psnode.cloud` and `certbot certonly --webroot -w /var/www/html -d login.psnode.cloud` (nginx needs a temporary port-80 site serving `/var/www/html` for this first run, since the full config refers to certificates that don't exist yet), copy `deploy/nginx.conf` to `/etc/nginx/sites-available/gurotopia`, link it into `sites-enabled`, remove the `default` site, and run `nginx -t && systemctl reload nginx`. nginx answers on 443 for both the website and the game's login request, and sends each to the right place.
8. **Firewall**
   ```
   ufw allow OpenSSH && ufw allow 80/tcp && ufw allow 443/tcp
   ufw allow 17091/udp && ufw allow 17100:17999/udp
   ufw enable
   ```
9. Sign up on the website, then make yourself an admin:
   `cd /opt/gurotopia-web && node --env-file=/etc/gurotopia/panel.env src/cli.js make-admin <username>` (run as root: `panel.env` is root-only, so game servers can't read the panel's database password)

Useful commands: `journalctl -u gurotopia-supervisor -f` (supervisor), `journalctl -u gurotopia-gateway -f` (gateway), `tail -f /srv/gurotopia/servers/<id>/logs/server.log` (one server).

**Updating the website**: `git pull` in `/opt/gurotopia-src`, copy `web/` over `/opt/gurotopia-web` (keep `node_modules`), run `npm ci --omit=dev`, then `systemctl restart gurotopia-panel`. The game servers keep running. Only restart `gurotopia-supervisor` when its own code changed, because that stops every server (they save first and start again by themselves).

## Custom item textures (Resources page)

Owners upload a PNG or .rttex on a server's Resources page. PNGs are converted to .rttex (padded to a
power-of-two size, cells stay where they were). Files are saved as `CDN_DIR/<server id>/cache/game/ps<id>_<name>_<hash>.rttex`;
the hash in the name means a changed picture always gets a new name, so players never keep a stale copy.

To let players download them:

1. `install -d -o gurotopia -g gurotopia -m 755 /srv/gurotopia/www /srv/gurotopia/www/cdn`
2. Set `CDN_HOST=panel.psnode.cloud` (and `CDN_DIR` if not the default) in `/etc/gurotopia/panel.env`.
3. Install the new `gurotopia-panel.service` (it may write to CDN_DIR) and the `/cdn/` blocks from `deploy/nginx.conf`,
   then `systemctl daemon-reload`, restart the panel and supervisor, and `nginx -t && systemctl reload nginx`.

Game servers then start with `--cdn-host CDN_HOST --cdn-path cdn/<id>/cache/`, and the client downloads any file it
doesn't have from there. nginx serves the server's own uploads and fetches everything else from Growtopia's CDN.

## Security notes

- Passwords are hashed with bcrypt. Logins are limited to 10 tries per 15 minutes per IP and username, and sign-ups to 5 per hour per IP.
- Every form that changes something carries a per-session CSRF token. Cookies are `HttpOnly` and `SameSite=Lax`, and `Secure` once `COOKIE_SECURE=true`.
- Owners only ever see their own servers. Anyone else gets "not found", whether the server exists or not.
- Server names are 3 to 24 letters and digits, case-insensitive and unique. Some names are reserved (admin, gateway, growtopia, ...), and there is a small blocklist of slurs in `src/servers.js`. Extend that list before you open sign-ups to the public.
- The supervisor never passes the panel's database password or token to game servers.

## Not done yet

- Lua scripts: each server folder already has an empty `scripts/` folder for them. The upload page and editor come once the game can run scripts.
- Per-server CPU and memory limits (systemd or cgroups), so one busy server can't slow down the rest.
- Nightly backups of each `srv_<id>` database, with a restore button.
- Moving the player list, bans and the message of the day onto the website.
- Password reset by email. For now an admin resets it with `src/cli.js reset-password <username> <new password>`.
