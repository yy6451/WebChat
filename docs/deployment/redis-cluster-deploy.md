# Redis Cluster 三节点部署方案

> 适配 TinyWebServer 项目，3 台 VM + 6 个 Redis 实例 + 1 个 MySQL，跨 VM 交叉部署。

---

## 一、完整架构

```
┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
│        VM1           │  │        VM2           │  │        VM3           │
│ 192.168.118.131      │  │ 192.168.118.133      │  │ 192.168.118.134      │
│                      │  │                      │  │                      │
│ MySQL ◄──────────────┼──┼── 远程连 VM1 的 MySQL ──┼── 远程连 VM1 的 MySQL ──┤
│ WebServer (:9007)    │  │ WebServer (:9007)    │  │ WebServer (:9007)    │
│                      │  │                      │  │                      │
│ Redis Master-1(:7000)│  │ Redis Master-2(:7000)│  │ Redis Master-3(:7000)│
│ Redis Replica-3(:7001)│  │ Redis Replica-1(:7001)│  │ Redis Replica-2(:7001)│
└──────────────────────┘  └──────────────────────┘  └──────────────────────┘
```

| 服务 | 分布 | 说明 |
|------|------|------|
| MySQL | **仅 VM1** | VM2/VM3 远程连接 VM1 |
| Redis Cluster | 三台 VM 各 2 实例 | 3 Master + 3 Replica，交叉部署 |
| WebServer | 三台 VM 各 1 实例 | 各自连 Redis 集群 + VM1 的 MySQL |

---

## 二、前置条件

### 2.1 确认三台 VM IP

在每台 VM 上执行，记下结果：

```bash
hostname -I | awk '{print $1}'
```

假设结果：

| VM | IP |
|----|----|
| VM1 | 192.168.118.131 |
| VM2 | 192.168.118.133 |
| VM3 | 192.168.118.134 |

以下所有步骤中的 IP 请替换为你的实际值。

### 2.2 端口规划

| 端口 | 协议 | 用途 | 需开放的机器 |
|------|------|------|-------------|
| 9007 | TCP | WebServer HTTP/WS | 三台 |
| 7000 | TCP | Redis 服务端口 | 三台 |
| 7001 | TCP | Redis 服务端口 | 三台 |
| 17000 | TCP | Redis Cluster Bus (7000+10000) | 三台 |
| 17001 | TCP | Redis Cluster Bus (7001+10000) | 三台 |
| 3306 | TCP | MySQL | 仅 VM1 |

### 2.3 依赖安装（每台 VM）

```bash
# 编译工具链
sudo apt update && sudo apt install -y g++ make cmake libmysqlclient-dev libssl-dev

# Redis（如果还没有）
sudo apt install -y redis-server

# redis-plus-plus（如果链接报错找不到）
# 项目已通过 find_library 自动搜索，通常无需手动安装
```

---

## 三、操作步骤总览

```
步骤1：代码分发        VM1 → scp → VM2, VM3
步骤2：MySQL 初始化    仅 VM1
步骤3：MySQL 远程授权   仅 VM1
步骤4：Redis 实例部署   三台 VM 各执行一次
步骤5：Redis 集群创建   任意 VM 执行一次
步骤6：项目编译         三台 VM 各执行一次
步骤7：启动 WebServer   三台 VM 各执行一次
步骤8：验证             任意 VM
```

---

## 四、详细操作

### 步骤 1：代码分发（VM1 执行）

将完整项目从 VM1 拷贝到 VM2 和 VM3：

```bash
# 在 VM1 上执行（替换实际 IP 和用户名）
scp -r ~/TinyWebServer yy1@192.168.118.133:~/
scp -r ~/TinyWebServer yy1@192.168.118.134:~/
```

如果 VM2/VM3 用户名不同，请相应修改。

---

### 步骤 2：MySQL 初始化（仅 VM1）

```bash
# 确保 MySQL 已启动
sudo systemctl start mysql
sudo systemctl enable mysql   # 开机自启

# 创建数据库和表
cd ~/TinyWebServer
mysql -u root < database_init.sql

# 或者用项目账号导入
mysql -u yy1 -p666888 < database_init.sql
```

> **注意**：`database_init.sql` 包含 4 张表（user / friend_relation / friend_request / message_log），含 `CREATE TABLE IF NOT EXISTS`，可安全重复执行。

---

### 步骤 3：MySQL 远程授权（仅 VM1）

```sql
-- 登录 MySQL
sudo mysql

-- 创建远程访问账号
CREATE USER 'yy1'@'192.168.118.%' IDENTIFIED BY '666888';
GRANT ALL PRIVILEGES ON mydb.* TO 'yy1'@'192.168.118.%';
FLUSH PRIVILEGES;

-- 确认 bind-address 允许外部访问
-- 编辑 /etc/mysql/mysql.conf.d/mysqld.cnf 或 /etc/mysql/my.cnf
-- 找到 bind-address = 127.0.0.1，改为：
-- bind-address = 0.0.0.0
-- 然后重启 MySQL：
-- sudo systemctl restart mysql
```

验证远程连接（在 VM2 或 VM3 上执行）：

```bash
mysql -h 192.168.118.131 -u yy1 -p666888 -e "SELECT 1"
```

---

### 步骤 4：Redis 实例部署（三台 VM 各执行一次）

在 **每台 VM** 上分别执行：

```bash
cd ~/TinyWebServer
./backend/scripts/run_redis.sh cluster
```

脚本自动完成：

- 创建 `/opt/redis-cluster/{7000,7001}` 数据目录
- 生成完整配置文件（AOF + cluster-announce-ip = 本机 IP）
- 停止旧实例 → 启动 redis-server :7000 和 :7001
- 输出本机 IP 和部署结果

验证每台 VM 的 Redis 是否正常：

```bash
redis-cli -h 192.168.118.131 -p 7000 PING   # 应返回 PONG
redis-cli -h 192.168.118.131 -p 7001 PING
redis-cli -h 192.168.118.133 -p 7000 PING
redis-cli -h 192.168.118.133 -p 7001 PING
redis-cli -h 192.168.118.134 -p 7000 PING
redis-cli -h 192.168.118.134 -p 7001 PING
```

---

### 步骤 5：Redis 集群创建（任意 VM 执行一次）

```bash
cd ~/TinyWebServer
./backend/scripts/cluster_create.sh 192.168.118.131 192.168.118.133 192.168.118.134
```

脚本自动完成：

1. 检查 6 个节点是否全部可达
2. 执行 `redis-cli --cluster create`：
   - 前 3 个节点（131:7000, 133:7000, 134:7000）作为 Master
   - 后 3 个节点（131:7001, 133:7001, 134:7001）作为 Replica
   - `--cluster-replicas 1`：每个 Master 配 1 个 Replica
3. 输出集群状态和节点列表

---

### 步骤 6：项目编译（三台 VM 各执行一次）

```bash
cd ~/TinyWebServer
rm -rf build
cmake -S . -B build
cmake --build build -j
```

---

### 步骤 7：启动 WebServer（三台 VM 各执行一次）

**VM1**（MySQL 在本机）：

```bash
cd ~/TinyWebServer
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -I 192.168.118.131 -t 8 -m 1
```

**VM2**（MySQL 远程连 VM1）：

```bash
cd ~/TinyWebServer
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -M 192.168.118.131 -I 192.168.118.133 -t 8 -m 1
```

**VM3**（同 VM2）：

```bash
cd ~/TinyWebServer
./build/backend/server -p 9007 -d 1 -H 60 -R 192.168.118.131 -P 7000 -M 192.168.118.131 -I 192.168.118.134 -t 8 -m 1
```

各参数含义：

| 参数 | 值 | 说明 |
|------|-----|------|
| `-p` | 9007 | WebServer 监听端口 |
| `-d` | 1 | 启用 MySQL 双写模式 |
| `-H` | 60 | 心跳超时 60 秒 |
| `-R` | 192.168.118.131 | Redis Cluster 任意节点 IP |
| `-P` | 7000 | Redis 端口 |
| `-M` | localhost / 192.168.118.131 | MySQL 主机地址 |
| `-I` | 本机 IP | 本机对外 IP（跨服消息去重标识）|
| `-t` | 8 | 线程池线程数 |
| `-m` | 0 | 并发模型 0=Proactor 1=Reactor |

---

### 步骤 8：验证

#### 8.1 验证 Redis Cluster

```bash
# 集群状态
redis-cli -h 192.168.118.131 -p 7000 CLUSTER INFO | head -3

# 节点列表（确认 replica 交叉部署到不同 VM）
redis-cli -h 192.168.118.131 -p 7000 CLUSTER NODES

# 写入测试（-c 启用集群重定向）
redis-cli -h 192.168.118.131 -p 7000 -c SET test hello
redis-cli -h 192.168.118.131 -p 7000 -c GET test
```

预期 `CLUSTER NODES` 输出：

```
VM1_IP:7000  master - 0-5460
VM2_IP:7000  master - 5461-10922
VM3_IP:7000  master - 10923-16383
VM2_IP:7001  slave of VM1_IP:7000    ← replica 在 VM2 而非 VM1
VM3_IP:7001  slave of VM2_IP:7000    ← replica 在 VM3 而非 VM2
VM1_IP:7001  slave of VM3_IP:7000    ← replica 在 VM1 而非 VM3
```

#### 8.2 验证 WebServer

```bash
# 三台都测试
curl http://192.168.118.131:9007/ | head -1
curl http://192.168.118.133:9007/ | head -1
curl http://192.168.118.134:9007/ | head -1

# 返回 <!doctype html> 表示正常
```

#### 8.3 验证注册/登录（数据共享）

在任意 VM 上注册一个用户，在另一台 VM 上登录：

```bash
# 在 VM1 注册
curl -X POST http://192.168.118.131:9007/api/register \
  -d "username=testuser&password=123456&repassword=123456"

# 在 VM2 登录（应该成功，因为 MySQL 共享）
curl -X POST http://192.168.118.133:9007/api/login \
  -d "username=testuser&password=123456"
```

#### 8.4 验证好友关系跨 VM 同步

```bash
# VM1 上的用户 yy 查看好友
TOKEN=$(curl -s -X POST http://192.168.118.131:9007/api/login \
  -d "username=yy&password=123456" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
curl -s http://192.168.118.131:9007/api/friends -H "Authorization: Bearer $TOKEN"

# VM3 上的同一个用户应该看到相同的好友列表
curl -s http://192.168.118.134:9007/api/friends -H "Authorization: Bearer $TOKEN"
```

---

## 五、配置文件说明

### 5.1 Redis 配置文件（由脚本自动生成）

```
port 7000
bind 0.0.0.0
protected-mode no
daemonize yes
dir /opt/redis-cluster/7000
pidfile /opt/redis-cluster/7000/redis.pid
logfile /opt/redis-cluster/7000/redis.log

cluster-enabled yes
cluster-config-file /opt/redis-cluster/7000/nodes.conf
cluster-node-timeout 10000

cluster-announce-ip <本机IP>       # 自动获取
cluster-announce-port 7000
cluster-announce-bus-port 17000

appendonly yes
appendfilename "appendonly.aof"
appendfsync everysec
save ""
```

7001 端口的配置同理，端口和 bus 端口相应替换。

### 5.2 config.cpp 配置（代码内）

`sw::redis::RedisCluster` 客户端连上任意一个节点后，通过 `CLUSTER SLOTS` 自动发现全部节点，因此 `redis_host` 只需配一个 IP。MySQL 通过 `-M` 命令行参数指定：

```cpp
// config.cpp 默认值（VM2/VM3 通过 -M 覆盖）
redis_host = "127.0.0.1";
redis_port = 7000;
mysql_host = "localhost";
enable_db = 1;
```

---

## 六、脚本清单

| 文件 | 用途 | 执行位置 |
|------|------|---------|
| `backend/scripts/run_redis.sh standalone` | 本地单节点 Redis（开发用） | 一台 VM |
| `backend/scripts/run_redis.sh cluster` | 三节点集群模式 Redis 部署 | 三台 VM 各一次 |
| `backend/scripts/cluster_deploy_node.sh` | 单机双实例部署（被 run_redis.sh cluster 调用） | 三台 VM 各一次 |
| `backend/scripts/cluster_create.sh` | 集群初始化，6 节点组网 | 任意 VM 一次 |

---

## 七、容灾能力

| 故障场景 | 结果 |
|---------|------|
| 单个 Redis 实例宕机 | Replica 自动提升为 Master ✅ |
| 任意一台 VM 整机关机 | Redis Master 丢失，其 Replica 在其他 VM 自动接管 ✅ |
| VM1 关机 | MySQL 不可用，**所有写入暂停** ⚠️ |
| 两台 VM 同时宕机 | Redis Cluster 超过半数 Master 丢失，不可用 ❌ |

> MySQL 单点风险：当前架构 MySQL 只在 VM1。如需消除单点，可配置 MySQL 主从复制或 Galera Cluster（超出本文档范围）。

---

## 八、本地单节点开发

如果只有一台 VM 做开发测试：

```bash
# Redis
./backend/scripts/run_redis.sh

# 构建
cmake -S . -B build && cmake --build build -j

# 启动（本地 Redis + 本地 MySQL）
./build/backend/server -p 9007 -d 1 -H 60 -R 127.0.0.1 -P 7000
```

---

## 九、常见问题

| 问题 | 解决 |
|------|------|
| `Connection refused` (Redis) | 检查防火墙是否开放 7000/7001/17000/17001 |
| `Connection refused` (MySQL) | 检查 `bind-address=0.0.0.0` 和 GRANT 远程权限 |
| `Auth Redis init failed` | Redis 未启动或端口不一致，执行 `redis-cli -p 7000 PING` |
| `CLUSTERDOWN` | 检查所有节点是否都已启动，重新执行 `cluster_create.sh` |
| 创建集群时报 `Node already knows` | 清理 `/opt/redis-cluster/*/nodes.conf` 和 `appendonly.aof` 后重试 |
| 客户端连不上 | 确认 `bind 0.0.0.0` 和 `protected-mode no` 已配置 |
| MySQL 连接失败（VM2/VM3） | 确认 `-M 192.168.118.131` 参数已传，防火墙 3306 已开放 |
| 好友数据不一致 | `-d 1` 模式下数据以 MySQL 为准，检查 `friend_relation` 表 |
