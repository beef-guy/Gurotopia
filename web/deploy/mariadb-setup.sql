-- Run once as root:  sudo mariadb < mariadb-setup.sql
-- Change both passwords first.

CREATE DATABASE IF NOT EXISTS gurotopia_platform;

-- The game servers and the gateway (goes in /opt/gurotopia/mysql_login.txt)
CREATE USER IF NOT EXISTS 'gurotopia_game'@'localhost' IDENTIFIED BY 'CHANGE-ME-game';
GRANT ALL PRIVILEGES ON `gurotopia_platform`.* TO 'gurotopia_game'@'localhost';
GRANT ALL PRIVILEGES ON `srv\_%`.* TO 'gurotopia_game'@'localhost';

-- The website and supervisor (goes in /etc/gurotopia/panel.env). It drops srv_* databases when a server is deleted.
CREATE USER IF NOT EXISTS 'gurotopia_panel'@'localhost' IDENTIFIED BY 'CHANGE-ME-panel';
GRANT ALL PRIVILEGES ON `gurotopia_platform`.* TO 'gurotopia_panel'@'localhost';
GRANT ALL PRIVILEGES ON `srv\_%`.* TO 'gurotopia_panel'@'localhost';

FLUSH PRIVILEGES;
