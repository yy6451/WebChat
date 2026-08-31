-- 创建数据库
CREATE DATABASE IF NOT EXISTS mydb DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE mydb;

-- 用户表（更新结构以支持 ID 和 bcrypt 加密密码）
CREATE TABLE IF NOT EXISTS user (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '用户唯一ID',
    username VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名（唯一）',
    passwd VARCHAR(255) NOT NULL COMMENT '加密后的密码',
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- 创建用户名索引以加速查询
CREATE INDEX IF NOT EXISTS idx_username ON user(username);

-- 好友关系表（双向关系）
CREATE TABLE IF NOT EXISTS friend_relation (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    friend_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_friend(user_id, friend_id),
    KEY idx_user_id(user_id),
    KEY idx_friend_id(friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友关系表';

-- 好友请求表
CREATE TABLE IF NOT EXISTS friend_request (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT NOT NULL,
    to_user_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0=待处理,1=已接受,2=已拒绝',
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_friend_req(from_user_id, to_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友请求表';

-- 私聊消息持久化表（用于未读统计）
CREATE TABLE IF NOT EXISTS message_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT NOT NULL,
    to_user_id INT NOT NULL,
    msg_type VARCHAR(16) NOT NULL,
    content TEXT NOT NULL,
    is_read TINYINT NOT NULL DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_to_user_read(to_user_id, is_read)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息记录表';
