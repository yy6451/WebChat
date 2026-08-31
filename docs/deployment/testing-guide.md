# TinyWebServer 测试方法文档

> 覆盖 HTTP API / WebSocket / 并发压力 / Redis Cluster / MySQL 的完整测试方案。

---

## 一、测试前准备

### 1.1 确保服务运行

```bash
# 启动 Redis Cluster（如果还没跑）
./backend/scripts/run_redis.sh

# 启动 MySQL（如果还没跑）
sudo systemctl start mysql

# 编译
cmake -S . -B build && cmake --build build -j

# 启动服务（-d 1 启用 MySQL 双写）
./build/backend/server -p 9007 -d 1 -H 60 -R 127.0.0.1 -P 7000

# 验证
curl http://127.0.0.1:9007/ | head -1
# 应返回: <!doctype html>
```

### 1.2 测试账号准备

```bash
# 注册测试用户
curl -X POST http://127.0.0.1:9007/api/register \
  -d "username=tester&password=123456&repassword=123456"

# 登录获取 token（后续测试用）
curl -X POST http://127.0.0.1:9007/api/login \
  -d "username=tester&password=123456"
# 记下返回的 token 值
```


## 二、HTTP API 功能测试

### 2.1 用户注册

```bash
# 正常注册
curl -s -X POST http://127.0.0.1:9007/api/register \
  -d "username=alice&password=123456&repassword=123456"
# 预期: {"code":0,"msg":"Registration successful"}

# 重复注册
curl -s -X POST http://127.0.0.1:9007/api/register \
  -d "username=alice&password=123456&repassword=123456"
# 预期: {"code":1,"msg":"Username already exists"}

# 密码不一致
curl -s -X POST http://127.0.0.1:9007/api/register \
  -d "username=bob&password=123456&repassword=654321"
# 预期: {"code":1,"msg":"Passwords do not match"}

# 密码过短
curl -s -X POST http://127.0.0.1:9007/api/register \
  -d "username=eve&password=12&repassword=12"
# 预期: {"code":1,"msg":"Password must be at least 6 characters"}
```

### 2.2 用户登录

```bash
# 正常登录
curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=123456"
# 预期: {"code":0,"msg":"Login successful","data":{"token":"...","username":"alice"}}

# 未注册用户
curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=nobody&password=123456"
# 预期: {"code":1,"msg":"User not registered, please register first"}

# 密码错误
curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=wrong"
# 预期: {"code":1,"msg":"Invalid password"}

# 存储 token 为变量
TOKEN=$(curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=123456" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
echo "Token: $TOKEN"
```

### 2.3 用户信息

```bash
curl -s http://127.0.0.1:9007/api/userinfo \
  -H "Authorization: Bearer $TOKEN"
# 预期: {"code":0,"msg":"ok","data":{"userId":...,"username":"alice"}}

# 无 Token
curl -s http://127.0.0.1:9007/api/userinfo
# 预期: {"code":401,"msg":"Unauthorized"}
```

### 2.4 搜索用户

```bash
# 搜索已存在用户
curl -s "http://127.0.0.1:9007/api/searchuser?username=bob" \
  -H "Authorization: Bearer $TOKEN"
# 预期: {"code":0,...,"data":{"username":"bob","exists":true}}

# 搜索不存在用户
curl -s "http://127.0.0.1:9007/api/searchuser?username=ghost" \
  -H "Authorization: Bearer $TOKEN"
# 预期: {"code":0,...,"data":{"username":"ghost","exists":false}}
```

### 2.5 好友系统

```bash
# 创建第二个用户
curl -s -X POST http://127.0.0.1:9007/api/register \
  -d "username=bob&password=123456&repassword=123456"

# alice 向 bob 发送好友请求
curl -s -X POST http://127.0.0.1:9007/api/addfriend \
  -H "Authorization: Bearer $TOKEN" \
  -d "friend=bob"
# 预期: {"code":0,"msg":"Friend request sent"}

# bob 登录并查看好友请求
TOKEN_BOB=$(curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=bob&password=123456" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

curl -s http://127.0.0.1:9007/api/friendrequests \
  -H "Authorization: Bearer $TOKEN_BOB"
# 预期: {"code":0,"msg":"ok","data":["alice"]}

# bob 接受 alice 的好友请求
curl -s -X POST http://127.0.0.1:9007/api/verifyfriend \
  -H "Authorization: Bearer $TOKEN_BOB" \
  -d "friend=alice&action=accept"
# 预期: {"code":0,"msg":"Friend request accepted"}

# 查看好友列表
curl -s http://127.0.0.1:9007/api/friends \
  -H "Authorization: Bearer $TOKEN"
# 预期: {"code":0,...,"data":[{"username":"bob","unread":0}]}

# 不能重复添加好友
curl -s -X POST http://127.0.0.1:9007/api/addfriend \
  -H "Authorization: Bearer $TOKEN" \
  -d "friend=bob"
# 预期: {"code":1,"msg":"already friends"}

# 不能添加自己
curl -s -X POST http://127.0.0.1:9007/api/addfriend \
  -H "Authorization: Bearer $TOKEN" \
  -d "friend=alice"
# 预期: {"code":1,"msg":"cannot add yourself"}
```


## 三、WebSocket 聊天功能测试

### 3.1 单客户端基础测试

在你的 **tools/** 目录中已有测试脚本：

```bash
# 基础 WebSocket 测试（需要先获取 token）
node tools/ws_private_test.mjs
```

### 3.2 双客户端私聊测试

```bash
# 终端 1: alice 登录并连接 WebSocket
TOKEN_A=$(curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=123456" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

# 终端 2: bob 登录并连接 WebSocket
TOKEN_B=$(curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=bob&password=123456" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

# 使用浏览器打开两个标签页：
#   标签页1: http://127.0.0.1:9007  → 登录 alice
#   标签页2: http://127.0.0.1:9007  → 登录 bob
#   alice 选择 bob 发私聊，验证 bob 能否收到
#   bob 回复，验证 alice 能否收到
```

### 3.3 群聊测试

```bash
# 三个浏览器标签页分别登录 alice、bob、tester
# 任意一个在默认房间发消息
# 验证其他两个都能收到
```

### 3.4 跨服务器消息测试（三节点部署时）

```bash
# VM1: 启动服务 alice 登录
# VM2: 启动服务 bob 登录
# alice → bob 私聊，验证跨 VM 消息到达
```


## 四、并发压力测试

### 4.1 webbench — HTTP GET 压测（已有工具）

```bash
cd ~/TinyWebServer/test_pressure/webbench-1.5
make clean && make

# 100 并发, 5 秒
./webbench -c 100 -t 5 http://127.0.0.1:9007/

# 500 并发, 10 秒
./webbench -c 500 -t 10 http://127.0.0.1:9007/

# 1000 并发, 30 秒（注意 ulimit -n 是否足够）
./webbench -c 1000 -t 30 http://127.0.0.1:9007/
```

**关注指标**：
- Speed (pages/min)：吞吐量
- succeed / failed：成功率（应为 100%）
- 服务端 CPU / 内存（`top -p $(pgrep server)`）

### 4.2 ApacheBench (ab) — HTTP POST 压测

```bash
# 安装
sudo apt install -y apache2-utils

# 创建 POST 数据文件
echo "username=tester&password=123456" > /tmp/login_data.txt

# 1000 请求, 50 并发
ab -n 1000 -c 50 -p /tmp/login_data.txt \
  -T 'application/x-www-form-urlencoded' \
  http://127.0.0.1:9007/api/login

# 关注指标:
#   Requests per second (QPS)
#   Failed requests (应为 0)
#   Time per request (平均延迟)
```

### 4.3 wrk — 高性能 HTTP 压测

```bash
# 安装
sudo apt install -y wrk

# 基础压测
wrk -t 4 -c 100 -d 10s http://127.0.0.1:9007/

# API 登录压测（需要 Lua 脚本）
cat > /tmp/login.lua << 'EOF'
wrk.method = "POST"
wrk.body   = "username=tester&password=123456"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
EOF

wrk -t 2 -c 50 -d 10s -s /tmp/login.lua http://127.0.0.1:9007/api/login

# 关注指标:
#   Requests/sec (QPS)
#   Latency (Avg / Max)
#   Transfer/sec
```

### 4.4 极限并发测试

```bash
# 提高文件描述符限制
ulimit -n 65535

# 查看当前限制
ulimit -n

# 10000 并发测试
./webbench -c 10000 -t 10 http://127.0.0.1:9007/

# 同时监控服务端
top -p $(pgrep -f 'build/backend/server') -d 1
```


## 五、Redis 数据验证

### 5.1 查看当前数据

```bash
# 所有 key
redis-cli -p 7000 KEYS '*'

# 用户密码
redis-cli -p 7000 GET 'user:pwd:{alice}'

# 在线用户
redis-cli -p 7000 ZRANGE active_users 0 -1 WITHSCORES

# 好友关系
redis-cli -p 7000 SMEMBERS 'friend:relations:{alice}'

# Stream 消息（如有）
redis-cli -p 7000 XINFO STREAM chat:messages
redis-cli -p 7000 XREVRANGE chat:messages + - COUNT 5
```

### 5.2 Redis 持久化验证

```bash
# 检查 AOF 是否开启
redis-cli -p 7000 INFO persistence | grep aof

# 手动触发 RDB 快照
redis-cli -p 7000 BGSAVE

# 重启 Redis 后验证数据是否恢复
redis-cli -p 7000 SHUTDOWN
# ...重启 redis-server...
redis-cli -p 7000 KEYS '*'  # 应该还有数据
```

### 5.3 Redis Cluster 健康检查

```bash
redis-cli -p 7000 CLUSTER INFO
redis-cli -p 7000 CLUSTER NODES
redis-cli -p 7000 --cluster check 127.0.0.1:7000
```


## 六、MySQL 数据验证

```bash
# 查看用户表
mysql -u yy1 -p666888 -e "USE mydb; SELECT id, username, create_time FROM user;"

# 查看好友关系
mysql -u yy1 -p666888 -e "USE mydb; SELECT u1.username AS user, u2.username AS friend FROM friend_relation fr JOIN user u1 ON fr.user_id=u1.id JOIN user u2 ON fr.friend_id=u2.id WHERE fr.status=1;"

# 查看消息记录
mysql -u yy1 -p666888 -e "USE mydb; SELECT id, msg_type, from_user_id, to_user_id, LEFT(content,30) AS content, is_read FROM message_log ORDER BY id DESC LIMIT 10;"

# 验证 MySQL → Redis 双写一致性
# 比较 MySQL user 表与 Redis user:pwd:{x} 的数量
mysql -u yy1 -p666888 -e "USE mydb; SELECT COUNT(*) FROM user;"
redis-cli -p 7000 KEYS 'user:pwd:*' | wc -l
```


## 七、故障与边界测试

### 7.1 无 Token 访问受保护接口

```bash
for api in /api/userinfo /api/friends /api/addfriend /api/friendrequests /api/verifyfriend; do
  echo "--- $api ---"
  curl -s http://127.0.0.1:9007$api
  echo ""
done
# 预期全部返回 401 Unauthorized
```

### 7.2 Redis 宕机恢复

```bash
# 模拟 Redis 宕机
redis-cli -p 7000 SHUTDOWN

# 此时服务应正常工作（MySQL 兜底）
curl -s -X POST http://127.0.0.1:9007/api/login \
  -d "username=alice&password=123456"
# 预期: 登录成功（走 MySQL 验证）

# 重新启动 Redis 并验证数据恢复
./backend/scripts/run_redis.sh
# WebServer 重启后会自动从 MySQL 预热 Redis
```

### 7.3 大消息测试

```bash
# 发送 10KB 的群聊消息（通过 WebSocket）
# 验证消息不分片、不乱码
```

### 7.4 并发好友请求

```bash
# 20 个用户同时向同一个用户发送好友请求
for i in $(seq 1 20); do
  curl -s -X POST http://127.0.0.1:9007/api/register \
    -d "username=test$i&password=123456&repassword=123456" &
done
# 验证每个请求独立处理，无数据竞争
```


## 八、一键测试脚本

创建 `tools/test_all.sh`：

```bash
#!/usr/bin/env bash
# 一键测试脚本：HTTP API + Redis + MySQL
set -euo pipefail

BASE="http://127.0.0.1:9007"
PASS=0
FAIL=0

check() {
  local desc="$1"
  local expected="$2"
  local actual="$3"
  if echo "$actual" | grep -q "$expected"; then
    echo "  ✅ $desc"
    ((PASS++))
  else
    echo "  ❌ $desc"
    echo "     expected: $expected"
    echo "     got:      $actual"
    ((FAIL++))
  fi
}

echo "===== 1) 注册测试 ====="
R=$(curl -s -X POST "$BASE/api/register" -d "username=ztest1&password=123456&repassword=123456")
check "新用户注册" '"code":0' "$R"

R=$(curl -s -X POST "$BASE/api/register" -d "username=ztest1&password=123456&repassword=123456")
check "重复注册拒绝" 'exists' "$R"

echo "===== 2) 登录测试 ====="
R=$(curl -s -X POST "$BASE/api/login" -d "username=ztest1&password=123456")
check "正常登录" '"code":0' "$R"
TOKEN=$(echo "$R" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

R=$(curl -s -X POST "$BASE/api/login" -d "username=ghost_nonexist&password=123456")
check "不存在用户拒绝" 'not registered' "$R"

R=$(curl -s -X POST "$BASE/api/login" -d "username=ztest1&password=wrong")
check "密码错误拒绝" 'Invalid password' "$R"

echo "===== 3) Redis 数据验证 ====="
COUNT=$(redis-cli -p 7000 KEYS 'user:pwd:*' 2>/dev/null | wc -l)
if [ "$COUNT" -gt 0 ]; then echo "  ✅ Redis 有 $COUNT 个用户记录"; else echo "  ❌ Redis 无数据"; fi

echo "===== 4) MySQL 数据验证 ====="
COUNT=$(mysql -u yy1 -p666888 -N -e "USE mydb; SELECT COUNT(*) FROM user;" 2>/dev/null)
if [ "$COUNT" -gt 0 ]; then echo "  ✅ MySQL 有 $COUNT 个用户记录"; else echo "  ❌ MySQL 无数据"; fi

echo ""
echo "========== 结果 =========="
echo "  通过: $PASS"
echo "  失败: $FAIL"
```

使用：

```bash
chmod +x tools/test_all.sh
./tools/test_all.sh
```


## 九、测试结果记录模板

| 测试日期 | 测试项 | 参数 | QPS | 成功率 | CPU | 内存 | 备注 |
|---------|-------|------|-----|--------|-----|------|------|
| 2026-06-17 | webbench | c=100 t=5 | 6051 | 100% | ~15% | ~18MB | 单机, 2 核 |
| | webbench | c=500 t=10 | | | | | |
| | webbench | c=1000 t=10 | | | | | |
| | wrk login | c=50 t=10 | | | | | |


## 十、性能调优参数

如果压测结果不理想，可以调整：

| 参数 | 位置 | 默认值 | 建议 |
|------|------|--------|------|
| 线程池大小 | `-t` | 8 | 设为 CPU 核数 |
| epoll 事件数 | `webserver.h MAX_EVENT_NUMBER` | 10000 | 不变 |
| 最大连接数 | `webserver.h MAX_FD` | 65536 | 不变 |
| 心跳超时 | `-H` | 90 | 压测时可设大一些 |
| 文件描述符 | `ulimit -n` | 1024 | `ulimit -n 65535` |
| 监听 backlog | `listen(m_listenfd, 5)` | 5 | 可改为 128 |
| SO_REUSEPORT | 代码 | 未开启 | 多进程可开启 |
