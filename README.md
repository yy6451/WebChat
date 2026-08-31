

# WebChat

Linux 下的 **C++17 高性能 Web 服务器与分布式 WebSocket 聊天系统**,前后端分离,支持 HTTP/WebSocket 双协议、epoll 高并发、Redis Cluster 跨服务器消息互通与 MySQL 双写持久化。

## ✨ 功能特性

- 线程池 + 非阻塞 socket + epoll(ET/LT)并发模型
- Reactor / 模拟 Proactor 事件模型可选
- 状态机 HTTP 请求解析(GET/POST)+ 静态资源服务
- WebSocket 聊天室(群聊 / 私聊 / 好友系统)
- Redis Cluster + MySQL 双写架构,跨服务器消息总线
- 经典时间轮定时器(O(1))、同步/异步日志系统

## 🧰 技术栈

| 层 | 技术 |
|----|------|
| 语言 | C++17(后端)· Vue3 + TypeScript(前端) |
| 网络 | epoll(ET/LT)· 非阻塞 socket · Reactor/Proactor |
| 协议 | HTTP/1.1 · WebSocket(RFC 6455) |
| 存储 | MySQL(主存储)· Redis Cluster(热缓存 / 在线状态 / 消息总线) |
| 构建 | CMake · npm(Vite) |

## 📁 项目结构

```
backend/          # C++17 后端服务(核心源码)
frontend/         # Vue3 前端(聊天室 UI)
docs/             # 文档(API / 架构 / 部署 / 测试)
test_pressure/    # webbench 压测工具
tools/            # 测试脚本
backend/scripts/  # 部署/运维脚本
```

## 🚀 快速开始

### 前置条件

- **Redis**(默认 `127.0.0.1:7000`),先启动:

```bash
./backend/scripts/run_redis.sh
```

- **MySQL**(仅 `-d 1` 双写模式需要,见下文「数据存储」)

### 1) 后端（C++）

根目录统一构建（推荐）：

```bash
cmake -S . -B build
cmake --build build -j
./build/backend/server -p 9007 -d 0 -H 60 -R 127.0.0.1 -P 7000 -N 0
```

或使用一键构建脚本（会同时刷新根目录 ./server 链接）：

```bash
./build.sh
./server -p 9007 -d 0 -H 60 -R 127.0.0.1 -P 7000 -N 0
```

后端目录构建：

```bash
cd backend
cmake -S . -B build
cmake --build build -j
./build/server -p 9007 -d 0 -H 60 -R 127.0.0.1 -P 7000 -N 0
```

参数说明：

- `-p`：端口（默认 9007）
- `-d`：数据库开关，`1=启用`（默认），`0=禁用`
- `-H`：心跳超时秒数（默认 90）
- `-t`：线程池线程数量（默认 8）
- `-R`：Redis Cluster 节点地址（默认 127.0.0.1）
- `-P`：Redis Cluster 节点端口（默认 7000）
- `-A`：Redis 认证密码（可选）
- `-N`：Redis DB 编号（Cluster 模式不支持 SELECT，仅兼容参数）
- `-M`：MySQL 主机地址（默认 localhost，远程可用 `-M 192.168.118.131`）
- `-I`：本服务器对外 IP（跨服务器消息去重用，三节点集群时启用）
- `-m`：触发组合模式，`1`=Reactor，`0`=Proactor（默认）

> 运行前请确保 Redis Cluster 已启动并可连通（可使用一键脚本）：

```bash
# 先启动 Redis Cluster（只需一次，重启机器后需重新运行）
./backend/scripts/run_redis.sh

# 再启动服务
./build/backend/server -p 9007 -d 0 -H 60 -R 127.0.0.1 -P 7000 -N 0
```

### 运行模式

| 模式 | `-d` 参数 | 用户密码存储 | 好友关系存储 | Redis 持久化 | 重启丢数据？ |
|------|----------|-------------|-------------|-------------|-------------|
| 纯 Redis | `0` | Redis `user:pwd:{x}` | Redis Set | ❌ 默认关闭 | ✅ 会丢 |
| MySQL 双写 | `1`（默认）| MySQL `user` 表 → 同步 Redis | MySQL `friend_relation` → 同步 Redis | ✅ MySQL 持久化 | ❌ 不丢 |

**`-d 1` 双写模式说明**（v2 新增）：
- 运行时每次写操作**同时写入 MySQL 和 Redis**，MySQL 做主存储，Redis 做热缓存
- 启动时自动从 MySQL 加载用户和好友数据到 Redis（预热）
- 即使 Redis 重启，数据也不丢失；MySQL 是持久化兜底
- **登录认证策略**：必须先注册才能登录，未注册账号会提示 "User not registered, please register first"

### 2) 前端（Node.js 18+）

```bash
cd frontend
npm install
npm run dev
```

浏览器访问前端开发地址（终端会提示），后端默认端口为 9007。

## 🗄️ 数据存储（MySQL）

> **注意**：`-d 0` 纯 Redis 模式不需要 MySQL；仅 `-d 1` 双写模式需要。

### 初始化数据库

```bash
# 1. 确保 MySQL 已启动
sudo systemctl start mysql

# 2. 创建数据库和表（用 root）
mysql -u root < database_init.sql

# 3.（可选）创建项目默认账号 test
mysql -u root -e "CREATE USER IF NOT EXISTS 'test'@'localhost' IDENTIFIED BY 'test123456';
GRANT ALL PRIVILEGES ON mydb.* TO 'test'@'localhost';
FLUSH PRIVILEGES;"
```

> 项目默认使用账号 `test` / `test123456`（`backend/src/main.cpp` 中设置，可用脚本环境变量覆盖）。若你本机使用 root 或其他账号，直接替换命令中的用户名即可。

### 测试数据（可选）

一键导入测试账号（alice / bob）：

```bash
./backend/scripts/mysql_seed_demo.sh
# 默认账号 test/test123456，可用环境变量覆盖：
# MYSQL_USER=root MYSQL_PASS=你的密码 ./backend/scripts/mysql_seed_demo.sh
```

### 查看数据库数据

```bash
# 进入交互式命令行
mysql -u test -ptest123456 mydb

# 或单条查询
mysql -u test -ptest123456 -e "USE mydb; SELECT id, username, LEFT(passwd,20) AS passwd, create_time FROM user;"
mysql -u test -ptest123456 -e "USE mydb; SELECT u1.username AS user, u2.username AS friend, fr.status FROM friend_relation fr JOIN user u1 ON fr.user_id=u1.id JOIN user u2 ON fr.friend_id=u2.id;"
mysql -u test -ptest123456 -e "USE mydb; SELECT * FROM friend_request;"
mysql -u test -ptest123456 -e "USE mydb; SELECT * FROM message_log ORDER BY id DESC LIMIT 20;"
```

## WebSocket 消息协议

聊天消息与系统消息统一为 JSON：

```json
{
    "type": "chat|heartbeat|system",
    "from": "user_xxx",
    "to": "room_default",
    "content": "hello",
    "timestamp": 1718000000,
    "seq": 1
}
```

## 文档

文档按主题分类组织,入口见 [docs/README.md](docs/README.md)。

- 接口与协议：
  - [docs/api/http-api.md](docs/api/http-api.md) — HTTP API 文档
  - [docs/api/ws-protocol.md](docs/api/ws-protocol.md) — WebSocket 消息协议
- 架构设计：
  - [docs/architecture/distributed-chat.md](docs/architecture/distributed-chat.md) — 分布式聊天室系统架构
  - [docs/architecture/cross-server-chat.md](docs/architecture/cross-server-chat.md) — 跨服务器聊天互通方案
- 部署与运维：
  - [docs/deployment/deployment-guide.md](docs/deployment/deployment-guide.md) — 详细部署指南
  - [docs/deployment/redis-cluster-deploy.md](docs/deployment/redis-cluster-deploy.md) — Redis Cluster 三节点部署
  - [docs/deployment/testing-guide.md](docs/deployment/testing-guide.md) — 完整测试方法文档
- 项目结构：
  - [docs/project/project-structure.md](docs/project/project-structure.md) — 项目结构与构建说明

## 压测

```bash
# webbench（已有工具，测 HTTP GET 静态页面吞吐）
cd test_pressure/webbench-1.5 && make && ./webbench -c 100 -t 5 http://127.0.0.1:9007/

# ApacheBench（测 HTTP POST API 并发，需安装 apache2-utils）
ab -n 1000 -c 50 -p /tmp/login_data.txt -T 'application/x-www-form-urlencoded' http://127.0.0.1:9007/api/login

# 并行 webbench（突破单进程瓶颈）
for i in $(seq 1 4); do ./webbench -c 200 -t 10 http://127.0.0.1:9007/ & done; wait
```

详见 [docs/deployment/testing-guide.md](docs/deployment/testing-guide.md)。

## 性能调优

| 调优点 | 位置 | 默认 | 建议 |
|--------|------|------|------|
| listen backlog | `webserver.cpp` | `listen(m_listenfd, 5)` | 建议调大至 128+ |
| 线程池大小 | `-t` 参数 | 8 | 设为 CPU 核数 |
| Reactor 模式 | `-m` 参数 | 0 (Proactor) | `-m 1` 提升 IO 并发 |
| 文件描述符 | `ulimit -n` | 1024 | `ulimit -n 65535` |

## 🏗️ 核心模块

```
ChatService → MessageDispatcher → 本地: ChatRoom (WebSocket 连接集合)
                                → 远程: ChatMessageBus → XADD chat:messages
                                     ↑
                              OnlineUserManager (全集群在线状态)
```

## 🗓️ 最近更新

| 日期 | 更新内容 |
|------|---------|
| 2026-06-17 | 并发测试文档 + webbench 压测验证（6051 QPS @ 100 并发） |
| 2026-06-16 | 分布式聊天室架构设计 + 跨服务器消息总线完整实现 |
| 2026-06-16 | RedisClient 扩展 Stream 命令（xadd/xreadgroup/xack/xtrim） |
| 2026-06-16 | OnlineUserManager + MessageDispatcher + MessageCodec 模块 |
| 2026-06-15 | Redis Cluster 三节点一键部署脚本 + 完整部署文档 |
| 2026-06-15 | 数据库建表统一至 database_init.sql，移除代码内建表 |
| 2026-06-12 | MySQL host 命令行可配置 (`-M`)，支持远程连接 |
| 2026-06-11 | 定时器从排序链表重构为经典时间轮（O(1) 操作） |
| 2026-06-11 | MySQL 双写架构：用户/好友 MySQL→Redis 同步 + 启动预热 |
| 2026-06-11 | 登录安全策略：未注册账号禁止登录 |

## 许可

本项目使用 [Apache License 2.0](LICENSE)。
