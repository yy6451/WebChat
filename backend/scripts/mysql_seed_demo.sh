#!/usr/bin/env bash
set -euo pipefail

MYSQL_HOST=${MYSQL_HOST:-127.0.0.1}
MYSQL_PORT=${MYSQL_PORT:-3306}
MYSQL_USER=${MYSQL_USER:-test}
MYSQL_PASS=${MYSQL_PASS:-test123456}
MYSQL_DB=${MYSQL_DB:-mydb}

MYSQL_BASE=(mysql -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_USER" -p"$MYSQL_PASS")

echo "[1/5] 初始化数据库与表..."
"${MYSQL_BASE[@]}" <<SQL
CREATE DATABASE IF NOT EXISTS ${MYSQL_DB} DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ${MYSQL_DB};
CREATE TABLE IF NOT EXISTS user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    passwd VARCHAR(255) NOT NULL,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS friend_relation (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    friend_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_friend(user_id, friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS message_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT NOT NULL,
    to_user_id INT NOT NULL,
    msg_type VARCHAR(16) NOT NULL,
    content TEXT NOT NULL,
    is_read TINYINT NOT NULL DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_to_user_read(to_user_id, is_read)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
SQL

echo "[2/5] 生成 PBKDF2 密码哈希..."
readarray -t HASH_LINES < <(python3 - <<'PY'
import os, hashlib

def mk(pwd):
    it = 10000
    salt = os.urandom(16).hex()
    h = hashlib.pbkdf2_hmac('sha256', pwd.encode(), bytes.fromhex(salt), it, dklen=32).hex()
    print(f"{it}${salt}${h}")

mk('123456')
mk('123456')
PY
)
ALICE_HASH=${HASH_LINES[0]}
BOB_HASH=${HASH_LINES[1]}

echo "[3/5] 写入测试用户 alice/bob ..."
"${MYSQL_BASE[@]}" <<SQL
USE ${MYSQL_DB};
INSERT INTO user(username, passwd) VALUES('alice', '${ALICE_HASH}') ON DUPLICATE KEY UPDATE passwd=VALUES(passwd);
INSERT INTO user(username, passwd) VALUES('bob', '${BOB_HASH}') ON DUPLICATE KEY UPDATE passwd=VALUES(passwd);
SQL

echo "[4/5] 建立好友关系并注入 3 条 bob->alice 未读私聊..."
"${MYSQL_BASE[@]}" <<SQL
USE ${MYSQL_DB};
SET @alice_id := (SELECT id FROM user WHERE username='alice' LIMIT 1);
SET @bob_id := (SELECT id FROM user WHERE username='bob' LIMIT 1);
INSERT IGNORE INTO friend_relation(user_id, friend_id, status) VALUES(@alice_id, @bob_id, 1),(@bob_id, @alice_id, 1);
INSERT INTO message_log(from_user_id, to_user_id, msg_type, content, is_read) VALUES
(@bob_id, @alice_id, 'private', 'hello-1', 0),
(@bob_id, @alice_id, 'private', 'hello-2', 0),
(@bob_id, @alice_id, 'private', 'hello-3', 0);
SQL

echo "[5/5] 完成。你现在可以启动服务并执行 unread 演示脚本。"