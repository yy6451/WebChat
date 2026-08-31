# WebChat 部署与使用指南

> 适配最新项目架构，覆盖单机开发部署和三节点集群生产部署。

---

## 一、项目概述

WebChat 是一个 C++17 高性能 Web 服务器 + WebSocket 聊天系统，技术栈：

- **网络层**：epoll (ET/LT) + 非阻塞 socket + Reactor/Proactor 模式
- **并发**：线程池 + 经典时间轮定时器 (O(1))
- **协议**：HTTP/1.1 + WebSocket (RFC 6455)
- **缓存**：Redis Cluster (3 Master + 3 Replica)
- **持久化**：MySQL 双写架构（用户 / 好友 / 消息日志）
- **消息总线**：Redis Streams + Consumer Groups（跨服务器通信）

---

## 二、环境要求

| 组件 | 版本要求 | 单机 | 三节点 |
|------|---------|:---:|:-----:|
| Linux | Ubuntu 20.04+ | ✅ | ✅ |
| g++ | C++17 | ✅ | ✅ |
| CMake | 3.10+ | ✅ | ✅ |
| MySQL | 5.7+ / 8.0+ | ✅ | 仅 VM1 |
| Redis | 5.0+ (cluster 模式) | ✅ | ✅ |
| libmysqlclient-dev | - | ✅ | ✅ |
| libssl-dev | - | ✅ | ✅ |
| Node.js | 18+ (前端开发) | 可选 | 可选 |

**安装依赖**（每台 VM）：

```bash
sudo apt update
sudo apt install -y g++ make cmake libmysqlclient-dev libssl-dev redis-server
```


## 三、单机开发部署

### 3.1 启动 Redis

```bash
# 一键启动单节点 Redis Cluster @ 7000
./backend/scripts/run_redis.sh

# 验证
redis-cli -p 7000 PING
# 应返回 PONG
```

### 3.2 初始化 MySQL（如启用 -d 1）

```bash
sudo systemctl start mysql
mysql -u root < database_init.sql
# 或用项目账号
mysql -u test -ptest123456 < database_init.sql
```

### 3.3 编译

```bash
cmake -S . -B build
cmake --build build -j
```

### 3.4 启动

```bash
# 纯 Redis 模式（-d 0）
./build/backend/server -p 9007 -d 0 -H 60 -R 127.0.0.1 -P 7000

# MySQL 双写模式（-d 1）
./build/backend/server -p 9007 -d 1 -H 60 -R 127.0.0.1 -P 7000

# Reactor + 8 线程（高并发推荐）
./build/backend/server -p 9007 -d 1 -H 60 -R 127.0.0.1 -P 7000 -t 8 -m 1
```

### 3.5 启动前端（可选）

```bash
cd frontend && npm install && npm run dev
```

浏览器访问 `http://127.0.0.1:9007`

### 3.6 验证

```bash
curl http://127.0.0.1:9007/ | head -1
# → <!doctype html>

curl -X POST http://127.0.0.1:9007/api/register \
  -d "username=alice&password=123456&repassword=123456"

curl -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=123456"
```


## 四、三节点集群部署

### 4.1 架构

```
┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
│        VM1           │  │        VM2           │  │        VM3           │
│ 192.168.118.131      │  │ 192.168.118.133      │  │ 192.168.118.134      │
│                      │  │                      │  │                      │
│ MySQL ◄──────────────┼──┼── 远程连 VM1 的 MySQL ──┼── 远程连 VM1 的 MySQL ──┤
│ WebServer (:9007)    │  │ WebServer (:9007)    │  │ WebServer (:9007)    │
│ Redis Master (:7000) │  │ Redis Master (:7000) │  │ Redis Master (:7000) │
│ Redis Replica(:7001) │  │ Redis Replica(:7001) │  │ Redis Replica(:7001) │
└──────────────────────┘  └──────────────────────┘  └──────────────────────┘
```

### 4.2 端口规划（三台 VM 均需开放）

| 端口 | 协议 | 用途 |
|------|------|------|
| 9007 | TCP | WebServer HTTP/WS |
| 7000 | TCP | Redis 服务端口 |
| 7001 | TCP | Redis 服务端口 |
| 17000 | TCP | Redis Cluster Bus |
| 17001 | TCP | Redis Cluster Bus |
| 3306 | TCP | MySQL（仅 VM1 对外） |

```bash
# 每台 VM
sudo ufw allow 9007/tcp
sudo ufw allow 7000/tcp
sudo ufw allow 7001/tcp
sudo ufw allow 17000/tcp
sudo ufw allow 17001/tcp
# VM1 额外:
sudo ufw allow 3306/tcp
```

### 4.3 步骤 1：代码分发

```bash
# 首次（VM1 执行，将 ubuntu 替换为你的 VM 登录用户名）
scp -r ~/TinyWebServer ubuntu@192.168.118.133:~/
scp -r ~/TinyWebServer ubuntu@192.168.118.134:~/

# 后续更新（只传差异）
rsync -avz --delete ~/TinyWebServer/ ubuntu@192.168.118.133:~/TinyWebServer/
rsync -avz --delete ~/TinyWebServer/ ubuntu@192.168.118.134:~/TinyWebServer/
```

### 4.4 步骤 2：MySQL（仅 VM1）

```bash
sudo systemctl start mysql
cd ~/TinyWebServer
mysql -u root < database_init.sql

# 授权远程访问
sudo mysql -e "
CREATE USER 'test'@'192.168.118.%' IDENTIFIED BY 'test123456';
GRANT ALL PRIVILEGES ON mydb.* TO 'test'@'192.168.118.%';
FLUSH PRIVILEGES;
"

# 修改 bind-address 允许外部连接
sudo sed -i 's/bind-address.*=.*/bind-address = 0.0.0.0/' /etc/mysql/mysql.conf.d/mysqld.cnf
sudo systemctl restart mysql

# 在 VM2/VM3 上验证
mysql -h 192.168.118.131 -u test -ptest123456 -e "SELECT 1"
```

### 4.5 步骤 3：Redis 实例（三台 VM）

每台 VM 执行：

```bash
cd ~/TinyWebServer
./backend/scripts/run_redis.sh cluster
```

验证 6 个节点：

```bash
redis-cli -h 192.168.118.131 -p 7000 PING
redis-cli -h 192.168.118.131 -p 7001 PING
redis-cli -h 192.168.118.133 -p 7000 PING
redis-cli -h 192.168.118.133 -p 7001 PING
redis-cli -h 192.168.118.134 -p 7000 PING
redis-cli -h 192.168.118.134 -p 7001 PING
# 全部应返回 PONG
```

### 4.6 步骤 4：创建集群（任意 VM 执行一次）

```bash
cd ~/TinyWebServer
./backend/scripts/cluster_create.sh 192.168.118.131 192.168.118.133 192.168.118.134
```

等待 15 秒收敛后验证：

```bash
redis-cli -h 192.168.118.131 -p 7000 CLUSTER INFO | head -3
# cluster_state:ok
```

### 4.7 步骤 5：编译（三台 VM）

```bash
cd ~/TinyWebServer
rm -rf build
cmake -S . -B build
cmake --build build -j
```

### 4.8 步骤 6：启动 WebServer（三台 VM）

**VM1**（本机 MySQL）：

```bash
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -I 192.168.118.131 -t 8 -m 1
```

**VM2**：

```bash
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -M 192.168.118.131 -I 192.168.118.133 -t 8 -m 1
```

**VM3**：

```bash
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -M 192.168.118.131 -I 192.168.118.134 -t 8 -m 1
```

### 4.9 验证

```bash
# 三台 Web 服务
curl http://192.168.118.131:9007/ | head -1
curl http://192.168.118.133:9007/ | head -1
curl http://192.168.118.134:9007/ | head -1

# 跨 VM 数据共享
curl -X POST http://192.168.118.131:9007/api/register \
  -d "username=crossvm&password=123456&repassword=123456"
curl -X POST http://192.168.118.133:9007/api/login \
  -d "username=crossvm&password=123456"
# → VM2 上应该能登录 VM1 注册的用户 ✅
```

### 4.10 Nginx 统一入口（一键配置）

使用脚本自动生成 Nginx stream 配置：

```bash
# 最少连接算法（推荐）
sudo ./backend/scripts/nginx_setup.sh least_conn

# 或 IP 哈希算法（生产环境）
sudo ./backend/scripts/nginx_setup.sh hash
```

脚本自动完成：生成 stream.conf → 禁用冲突的默认站点 → 追加 include → 测试并重启 Nginx → 验证代理返回 200。

用户访问 `http://192.168.118.131` 即可，无需关心后端是三台 VM。WebSocket 同样走 80 端口。

**IP 哈希分配机制详解**：

```
hash $remote_addr consistent;

alice (IP 192.168.1.100)
  → hash("192.168.1.100") = 固定值 → 映射到 VM2
  → alice 的所有 HTTP 请求 + WebSocket 连接永远走 VM2

bob (IP 192.168.1.200)
  → hash("192.168.1.200") = 另一个固定值 → 映射到 VM1
```

- **不是随机**：根据客户端 IP 做哈希计算，同一个 IP 永远映射到同一台后端。客户端 IP 不变，服务器就不变。
- **固定多久**：永久。只要后端服务器列表不变 + 客户端 IP 不变，分配结果永远不会变。刷新页面、关闭浏览器重开、甚至重启 Nginx，同一个客户端 IP 仍然到同一台后端。
- **某台宕机**：Nginx 检测到后端不可达时，自动将该后端标记为 `down`，原本映射到这台后端的客户端 IP 会被**重新哈希**分配到剩余的健康后端。`consistent` 参数保证只重新分配宕机那台的用户，其他用户分配不变。
- **宕机恢复**：Nginx 检测到后端恢复可达后，自动重新加入列表。原本属于这台服务器的用户 IP 会重新路由回这台服务器。
- **对聊天室的影响**：VM2 宕机时，原本连 VM2 的用户 WebSocket 断开 → 浏览器自动重连 → Nginx 重新分配到健康后端（如 VM1）→ 重建 ChatRoom 连接 → OnlineUserManager 更新 `online:server:{alice}` → 后续私聊正常送达。VM2 恢复后新连接重新分配到 VM2。


## 五、命令行参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `-p` | 9007 | 监听端口 |
| `-d` | 1 | 0=纯Redis 1=MySQL双写 |
| `-H` | 90 | 心跳超时(秒) |
| `-t` | 8 | 线程池大小 |
| `-m` | 0 | 0=Proactor 1=Reactor |
| `-R` | 127.0.0.1 | Redis host |
| `-P` | 7000 | Redis port |
| `-M` | localhost | MySQL host |
| `-I` | "" | 本机对外IP（跨服启用） |
| `-l` | 0 | 日志 0=同步 1=异步 |
| `-s` | 8 | 数据库连接池数 |


## 六、API 接口

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|:---:|
| POST | `/api/register` | 注册 | - |
| POST | `/api/login` | 登录返回 token | - |
| GET | `/api/userinfo` | 用户信息 | Bearer |
| GET | `/api/friends` | 好友列表 | Bearer |
| POST | `/api/addfriend` | 发好友请求 | Bearer |
| GET | `/api/friendrequests` | 待处理请求 | Bearer |
| POST | `/api/verifyfriend` | 接受/拒绝 | Bearer |
| GET | `/api/searchuser` | 搜索用户 | Bearer |
| POST | `/api/readfriend` | 标记已读 | Bearer |
| WS | `/chat?token=...` | WebSocket | Token |


## 七、Redis 数据结构

```
user:pwd:{username}              STRING     PBKDF2-SHA256
token:{token}                    STRING(TTL)
active_users                     ZSET       score=expireAt
friend:requests:{username}       SET
friend:relations:{username}      SET
online:server:{username}         STRING(TTL) "server_id"
chat:messages                    STREAM     MAXLEN~10000
chat:room:{roomId}:seq           INCR
```


## 八、MySQL 表结构

```sql
user              -- 用户密码
friend_relation   -- 双向好友关系
friend_request    -- 好友请求
message_log       -- 消息记录
```


## 九、性能调优

| 调优点 | 默认 | 建议 |
|--------|------|------|
| 线程池 | `-t 8` | = CPU 核数 |
| Reactor | `-m 0` | `-m 1` 高并发 |
| fd 限制 | 1024 | `ulimit -n 65535` |
| MySQL pool | `-s 8` | 并发量/2 |

```bash
# 内核调优（可选）
sudo sysctl -w net.core.somaxconn=65535
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=8192
```


## 十、压测

```bash
# webbench 静态页面
cd test_pressure/webbench-1.5 && make
./webbench -c 500 -t 10 http://127.0.0.1:9007/

# 并行 webbench
for i in 1 2 3 4; do ./webbench -c 200 -t 10 http://127.0.0.1:9007/ & done; wait
```

详见 [testing-guide.md](testing-guide.md)。


## 十一、维护命令

```bash
# 停止服务
fuser -k 9007/tcp

# 查看 Redis
redis-cli -p 7000 KEYS '*'
redis-cli -p 7000 XINFO STREAM chat:messages

# 查看 MySQL
mysql -u test -ptest123456 -e "USE mydb; SELECT COUNT(*) FROM user;"

# 查看日志
tail -f logs/ServerLog
```


## 十二、文档索引

| 文档 | 内容 |
|------|------|
| [../api/http-api.md](../api/http-api.md) | HTTP API 文档 |
| [../api/ws-protocol.md](../api/ws-protocol.md) | WebSocket 协议 |
| [redis-cluster-deploy.md](redis-cluster-deploy.md) | Redis Cluster 部署 |
| [../architecture/distributed-chat.md](../architecture/distributed-chat.md) | 分布式架构设计 |
| [testing-guide.md](testing-guide.md) | 测试方法 |
