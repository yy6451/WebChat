# 跨服务器聊天室互通方案（Redis Streams + Consumer Groups）

> **文档类型**：架构设计文档（设计阶段产物）
> **实际实现**：代码位于 `backend/src/module/chat/`（chat_message_bus, message_dispatcher, online_user_manager, message_codec）
> **⚠️ 注意**：实际代码与本文档的设计存在以下关键差异：
> - 本文档设计中 `ChatRoom::Broadcast()` 直接调用 `ChatMessageBus::publish()`；实际实现中消息路由通过 `MessageDispatcher` 回调机制完成
> - 本文档设计使用 `ChatRoom::onRemoteMessage()` 回调；实际实现使用 `MessageDispatcher::onBusMessage()` + 回调注入
> - 本文档设计使用 `simpleJsonField()` 手动解析；实际实现使用 `MessageCodec` 类统一编解码
> - 本文档设计中 `XREADGROUP` 返回多层嵌套 vector；实际实现中简化为 `vector<vector<pair<string,string>>>`
>
> 以下为原始设计文档内容，用于理解架构思路和面试准备。

> 适配 WebChat 三节点部署架构，实现不同 VM 上的用户之间实时消息互通。


## 一、当前问题

```
当前架构：ChatRoom 是进程内单例

┌─ VM1 ──────────┐  ┌─ VM2 ──────────┐  ┌─ VM3 ──────────┐
│ ChatRoom        │  │ ChatRoom        │  │ ChatRoom        │
│ connections_:   │  │ connections_:   │  │ connections_:   │
│  {alice}        │  │  {bob}          │  │  {charlie}      │
└─────────────────┘  └─────────────────┘  └─────────────────┘

alice → bob 私聊?
  ChatRoom::SendToUser("bob", msg)
    → 遍历 VM1 的 user_index_["bob"] → nullptr → 返回 false
    → "user not online" ❌
```

根因：`ChatRoom::user_index_` 是进程内 `std::unordered_map`，只能看到本机 WebSocket 连接。


## 二、为什么 Redis Streams（而非 Pub/Sub）

| 维度 | Pub/Sub | Streams + Consumer Groups |
|------|---------|--------------------------|
| 消息持久化 | ❌ 不持久化，消费者离线时丢失 | ✅ AOF 持久化到磁盘 |
| 重启恢复 | ❌ 丢失全部未消费消息 | ✅ XPENDING + 重放恢复 |
| 消费进度追踪 | ❌ 无，发完即忘 | ✅ 每个 Group 独立追踪最后消费 ID |
| 积压监控 | ❌ 无法查看 | ✅ XINFO GROUPS / XINFO STREAM |
| 消息回溯 | ❌ 不支持 | ✅ XREAD 从任意 ID 开始 |
| 复杂度 | 低 | 中等 |

**选 Streams 的核心理由**：服务器崩溃重启后不丢消息，这是生产环境的基础要求。


## 三、架构设计

### 3.1 整体架构

```
                          Redis Cluster
              ┌──────────────────────────────────┐
              │  Stream: chat:messages            │
              │    XADD  ← 发布消息（AOF 持久化）  │
              │    XREADGROUP ← 各服务器独立消费   │
              │    XACK  ← 确认消费完成            │
              │    XTRIM MAXLEN 10000 ← 自动裁剪   │
              │                                  │
              │  Consumer Groups（每服务器一个）:   │
              │    group:server-192.168.118.131   │
              │    group:server-192.168.118.133   │
              │    group:server-192.168.118.134   │
              └──────────────────────────────────┘
                   ↑            ↑            ↑
                   │            │            │
          ┌────────┴──┐  ┌─────┴─────┐  ┌──┴─────────┐
          │   VM1     │  │   VM2     │  │   VM3      │
          │ ChatRoom  │  │ ChatRoom  │  │ ChatRoom   │
          │ alice     │  │ bob       │  │ charlie    │
          └───────────┘  └───────────┘  └────────────┘
```

### 3.2 核心流程（alice@VM1 → bob@VM2）

```
VM1 (发送方):
  1. ChatRoom::Broadcast("hello")
  2. XADD chat:messages * data "hello" server_id "VM1"
     → 返回 msg-id: 1718000000000-0
  3. 本地 connections_ 遍历 → sendData()（本地用户立即收到）

VM2 (消费方，独立线程):
  4. XREADGROUP GROUP group:server-133 consumer-1
     BLOCK 1000 STREAMS chat:messages >
     → 阻塞等待，收到 msg-id: 1718000000000-0
  5. onRemoteMessage("hello")
  6. 本地查找 bob → sendData() ✅
  7. XACK chat:messages group:server-133 1718000000000-0

VM3: 同 VM2，独立消费同一消息

VM1 自己也收到:
  8. server_id 匹配 → 跳过（去重）
```

### 3.3 关键设计：为什么每服务器一个 Consumer Group？

```
错误设计：所有服务器共享一个 Consumer Group
  → XREADGROUP 会将消息只投递给组内一个消费者
  → bob 收到消息，charlie 收不到 ❌

正确设计：每个服务器有自己的 Consumer Group
  → 每个 Group 独立追踪消费进度
  → 所有服务器都收到同一条消息 ✅
  → 本质是利用 Consumer Group 的「进度追踪」能力
```

### 3.4 Stream 与 Consumer Group 命名

| 服务器 | Consumer Group | Consumer Name |
|--------|---------------|---------------|
| VM1 | `group:server-192.168.118.131` | `consumer-1` |
| VM2 | `group:server-192.168.118.133` | `consumer-1` |
| VM3 | `group:server-192.168.118.134` | `consumer-1` |

### 3.5 消息格式（兼容现有协议）

```json
{
  "__server_id": "192.168.118.131:9007",
  "type": "chat|private|system",
  "from": "alice",
  "to": "room_default|bob",
  "content": "hello",
  "timestamp": 1718000000,
  "seq": 42
}
```

`__server_id` 用于去重：发送服务器收到自己 XADD 的消息时跳过。


## 四、Redis Stream 命令封装（RedisClient 新增）

**文件**：`backend/src/utils/redis/redis_client.h`、`redis_client.cpp`

```cpp
class RedisClient {
public:
    // ...existing code...

    // XADD key * field value [field value ...]  → 返回消息 ID
    bool xadd(const std::string& key, const std::string& id,
              const std::vector<std::pair<std::string, std::string>>& fields,
              std::string* out_id, std::string* err);

    // XGROUP CREATE key group id [MKSTREAM]
    bool xgroup_create(const std::string& key, const std::string& group,
                       const std::string& start_id, bool mkstream, std::string* err);

    // XREADGROUP GROUP group consumer [COUNT count] [BLOCK ms] STREAMS key id
    // 返回: vector{stream_name, vector{msg_id, vector{field, value}}}
    bool xreadgroup(const std::string& group, const std::string& consumer,
                    const std::string& key, const std::string& id,
                    int count, int block_ms,
                    std::vector<std::pair<std::string,
                        std::vector<std::pair<std::string,
                            std::vector<std::pair<std::string, std::string>>>>>>& result,
                    std::string* err);

    // XACK key group id
    bool xack(const std::string& key, const std::string& group,
              const std::string& id, std::string* err);

    // XTRIM key MAXLEN ~ count
    bool xtrim_maxlen(const std::string& key, long long maxlen, std::string* err);

    // XPENDING key group - + 1
    long long xpending_count(const std::string& key, const std::string& group, std::string* err);
};
```


## 五、新建 ChatMessageBus 类

**文件**：`backend/src/module/chat/chat_message_bus.h`、`chat_message_bus.cpp`

职责：
1. 启动时创建 Consumer Group（幂等）
2. 独立线程循环 `XREADGROUP ... BLOCK 1000` 消费消息
3. 回调 `ChatRoom::onRemoteMessage()` 投递到 WebSocket
4. 提供 `publish()` 供 ChatRoom 调用（内部 XADD）

```cpp
class ChatMessageBus {
public:
    static ChatMessageBus* instance();

    // 初始化：连接 Redis → 创建 Consumer Groups → 启动消费线程
    bool init(const std::string& host, int port,
              const std::string& password, const std::string& serverId,
              std::string* err);

    // 发布消息到 Stream
    void publish(const std::string& stream, const std::string& message);

    void shutdown();
    const std::string& serverId() const { return server_id_; }

private:
    void consumeLoop();                          // 消费线程主循环
    void handleMessage(const std::string& msgId, const std::string& data);

    std::string server_id_;                      // "192.168.118.131:9007"
    std::string consumer_group_;                 // "group:server-192.168.118.131"
    std::string consumer_name_;                  // "consumer-1"
    RedisClient redis_;                          // Redis 连接
    std::thread consume_thread_;
    std::atomic<bool> running_{false};
};
```

### 5.1 init() 核心实现

```cpp
bool ChatMessageBus::init(const std::string& host, int port,
                          const std::string& password,
                          const std::string& serverId, std::string* err) {
    server_id_ = serverId;
    consumer_group_ = "group:server-" + serverId;
    consumer_name_ = "consumer-1";

    if (!redis_.connect(host, port, password, 0, err)) return false;

    // 幂等创建 Consumer Group（已存在则忽略 BUSYGROUP 错误）
    redis_.xgroup_create("chat:messages", consumer_group_, "$", true, err);

    running_ = true;
    consume_thread_ = std::thread(&ChatMessageBus::consumeLoop, this);
    return true;
}
```

### 5.2 publish() 核心实现

```cpp
void ChatMessageBus::publish(const std::string& stream, const std::string& message) {
    std::string msgId, err;
    redis_.xadd(stream, "*", {{"data", message}}, &msgId, &err);
    redis_.xtrim_maxlen(stream, 10000, &err);  // 裁剪旧消息
}
```

### 5.3 consumeLoop() 核心实现

```cpp
void ChatMessageBus::consumeLoop() {
    while (running_) {
        ResultType results;
        std::string err;
        if (!redis_.xreadgroup(consumer_group_, consumer_name_,
                "chat:messages", ">", 100, 1000, results, &err)) {
            if (running_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        for (auto& [stream, messages] : results) {
            for (auto& [msgId, fields] : messages) {
                std::string data;
                for (auto& [k, v] : fields) if (k == "data") data = v;
                if (!data.empty()) {
                    ChatRoom::instance()->onRemoteMessage(data);
                }
                redis_.xack("chat:messages", consumer_group_, msgId, &err);
            }
        }
    }
}
```


## 六、ChatRoom 改造

**文件**：`backend/src/module/chat/chat_room.h`、`chat_room.cpp`

### 6.1 头文件新增

```cpp
class ChatRoom {
public:
    // ...existing...

    void onRemoteMessage(const std::string& message);   // 消费线程回调
    void setServerId(const std::string& id) { server_id_ = id; }

private:
    std::string server_id_;
};
```

### 6.2 Broadcast 改造

```cpp
void ChatRoom::Broadcast(const std::string& message, WebSocketConnection* exclude) {
    // 1. 本地广播
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto conn : connections_) {
            if (conn != exclude)
                conn->sendData(message.c_str(), message.size());
        }
    }
    // 2. 发布到 Redis Stream（其他服务器消费线程处理）
    ChatMessageBus::instance()->publish("chat:messages", message);
}
```

### 6.3 SendToUser 改造

```cpp
bool ChatRoom::SendToUser(const std::string& username, const std::string& message) {
    // 1. 本地查找
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_index_.find(username);
        if (it != user_index_.end()) {
            it->second->sendData(message.c_str(), message.size());
            return true;
        }
    }
    // 2. 本地未找到 → XADD 到 Stream，其他服务器投递
    ChatMessageBus::instance()->publish("chat:messages", message);
    return true;
}
```

### 6.4 onRemoteMessage（消费线程回调）

```cpp
void ChatRoom::onRemoteMessage(const std::string& message) {
    std::string sender = simpleJsonField(message, "__server_id");
    if (sender == server_id_) return;   // 去重

    std::string to = simpleJsonField(message, "to");
    std::lock_guard<std::mutex> lock(mutex_);

    if (to == kDefaultRoomId) {
        for (auto conn : connections_)
            conn->sendData(message.c_str(), message.size());
    } else {
        auto it = user_index_.find(to);
        if (it != user_index_.end())
            it->second->sendData(message.c_str(), message.size());
    }
}
```


## 七、WebServer 初始化改造

**文件**：`backend/src/core/webserver.cpp`

```cpp
void WebServer::eventListen() {
    // ...existing code (socket, epoll, signal)... 

    // 初始化跨服务器消息总线
    std::string serverId = config.server_announce_ip + ":" + std::to_string(m_port);
    ChatRoom::instance()->setServerId(serverId);

    std::string err;
    if (!ChatMessageBus::instance()->init(
            config.redis_host, config.redis_port,
            config.redis_pass, serverId, &err)) {
        LOG_ERROR("ChatMessageBus init failed: %s — falling back to local-only mode", err.c_str());
        // 失败不阻塞启动，降级为本地模式
    }
}
```

### config.cpp 新增 -I 参数

```cpp
// config.h
std::string server_announce_ip;

// config.cpp 构造函数
server_announce_ip = "";

// parse_arg 新增
case 'I': {
    server_announce_ip = optarg;
    break;
}
```


## 八、消息流转完整时序（Streams 版本）

```
alice(VM1) 群聊 "hello":

  VM1                                     VM2                       VM3
  │                                        │                         │
  ├─ WS 收到消息                           │                         │
  ├─ ChatRoom::Broadcast()                 │                         │
  │   ├─ 本地: conn.sendData() ✅          │                         │
  │   └─ XADD chat:messages * data msg    │                         │
  │      └→ msg-001 写入 Stream (AOF)     │                         │
  ┌─ VM1 consumeLoop()                     │                         │
  │  XREADGROUP ... >                     │                         │
  │  ← 收到 msg-001                       │                         │
  │  onRemoteMessage()                    │                         │
  │  server_id match → SKIP ✅            │                         │
  │  XACK msg-001                         │                         │
  └────────────────────────────────────────│                         │
                        ┌─ VM2 consumeLoop()                         │
                        │  XREADGROUP ... >                         │
                        │  ← 收到 msg-001                           │
                        │  onRemoteMessage()                        │
                        │  本地广播 → bob ✅                        │
                        │  XACK msg-001                             │
                        └────────────────────────────────────────────│
                        │                                            │
                        │                       ┌─ VM3 consumeLoop()
                        │                       │  ← 收到 msg-001
                        │                       │  → charlie ✅
                        │                       │  XACK msg-001
                        │                       └────────────────────
```


## 九、Stream 生命周期管理

### 9.1 消息裁剪

```cpp
// 每次 XADD 后自动裁剪，保留最近 10000 条
XTRIM chat:messages MAXLEN ~ 10000
```

`~` = 近似裁剪，性能优于精确模式。

### 9.2 服务器恢复（崩溃重启）

```cpp
void ChatMessageBus::recoverPending() {
    // 1. 检查积压
    long long pending = redis_.xpending_count("chat:messages", consumer_group_, &err);

    if (pending > 0) {
        // 2. 从 Group 的 last_delivered_id 开始重放未确认消息
        ResultType results;
        redis_.xreadgroup(consumer_group_, consumer_name_,
            "chat:messages", "0", 100, 0, results, &err);
        // 处理积压消息后 XACK
    }
    // 3. 之后正常 XREADGROUP ... > 读新消息
}
```

### 9.3 Consumer Group 创建幂等性

```cpp
bool RedisClient::xgroup_create(...) {
    try {
        cluster_->command("XGROUP", "CREATE", key, group, start_id,
                          mkstream ? "MKSTREAM" : "");
    } catch (const sw::redis::ReplyError& e) {
        if (std::string(e.what()).find("BUSYGROUP") != std::string::npos) {
            return true;  // 已存在，幂等成功
        }
        if (err) *err = e.what();
        return false;
    }
    return true;
}
```


## 十、改造文件清单

| 文件 | 改动 | 说明 |
|------|------|------|
| `utils/redis/redis_client.h/.cpp` | 修改 | 新增 xadd/xgroup_create/xreadgroup/xack/xtrim/xpending |
| `module/chat/chat_message_bus.h/.cpp` | **新建** | Stream 消费总线，XREADGROUP 循环线程 |
| `module/chat/chat_room.h/.cpp` | 修改 | 新增 onRemoteMessage/setServerId |
| `core/webserver.cpp` | 修改 | eventListen 中初始化 ChatMessageBus |
| `config/config.h/.cpp` | 修改 | 新增 server_announce_ip + -I 参数 |
| `CMakeLists.txt` | 修改 | 添加 chat_message_bus.cpp |


## 十一、监控与调试

```bash
# 查看 Stream 基本信息
redis-cli -h 192.168.118.131 -p 7000 XINFO STREAM chat:messages
# 输出: length, radix-tree-keys, radix-tree-nodes, last-generated-id, groups, first-entry, last-entry

# 查看所有 Consumer Group 的消费进度
redis-cli -h 192.168.118.131 -p 7000 XINFO GROUPS chat:messages

# 查看 VM2 的消费者状态
redis-cli -h 192.168.118.131 -p 7000 XINFO CONSUMERS chat:messages group:server-192.168.118.133
# 输出: name, pending, idle (毫秒)

# 查看 VM2 的积压消息数
redis-cli -h 192.168.118.131 -p 7000 XPENDING chat:messages group:server-192.168.118.133

# 查看最新 10 条消息
redis-cli -h 192.168.118.131 -p 7000 XREVRANGE chat:messages + - COUNT 10

# 删除整个 Stream（重置）
redis-cli -h 192.168.118.131 -p 7000 DEL chat:messages
```


## 十二、注意事项

### 12.1 线程安全

- `consumeLoop()` 在独立线程运行，通过 `XREADGROUP BLOCK` 阻塞
- `onRemoteMessage()` 由消费线程调用，访问 `connections_`/`user_index_` 需要 `mutex_.lock()`
- `ChatRoom::mutex_` 已是 `mutable std::mutex`

### 12.2 阻塞与关闭

- `XREADGROUP BLOCK 1000` 最多阻塞 1 秒
- `shutdown()` 设置 `running_=false` → 1 秒内 `XREADGROUP` 超时返回 → 线程退出
- `consume_thread_.join()` 等待线程安全退出

### 12.3 消息丢失边界

| 场景 | 丢失？ | 如何恢复 |
|------|--------|---------|
| VM2 正在消费时崩溃 | ❌ 不丢 | 重启后 XPENDING 检查 → 从 "0" 重放未 ACK 的消息 |
| VM2 崩溃时 VM1 发了新消息 | ❌ 不丢 | 消息在 Stream 中持久化，VM2 重启后读到 |
| Redis 全集群崩溃 | ✅ 可能丢 | Stream 有 AOF 持久化（配置了 appendonly yes） |

### 12.4 降级策略

- `ChatMessageBus::init()` 失败 → 回退纯本地模式
- 单机开发时不传 `-I` → server_id 为空 → 不启动消费线程
- 完全向后兼容

### 12.5 性能基线

```
单条消息延迟: XADD < 1ms（本地 Redis）
              XREADGROUP BLOCK 1000 在 idle 时无 CPU 开销
              XACK < 1ms
Stream 大小:   10000 条 × 512B ≈ 5MB（可忽略）
消费吞吐:     单线程 10000 msg/s（受 redis++ 客户端限制）
```


## 十三、配置示例

```bash
# VM1
./build/backend/server -p 9007 -d 1 -H 60 \
  -R 192.168.118.131 -P 7000 \
  -I 192.168.118.131

# VM2
./build/backend/server -p 9007 -d 1 -H 60 \
  -R 192.168.118.131 -P 7000 \
  -M 192.168.118.131 \
  -I 192.168.118.133

# VM3
./build/backend/server -p 9007 -d 1 -H 60 \
  -R 192.168.118.131 -P 7000 \
  -M 192.168.118.131 \
  -I 192.168.118.134
```


## 十四、Pub/Sub vs Streams 差异速查

| 维度 | Pub/Sub | Streams（本方案） |
|------|---------|------------------|
| 发送 | `PUBLISH channel msg` | `XADD stream * data msg` |
| 消费 | `SUBSCRIBE` 回调推送 | `XREADGROUP BLOCK` 轮询拉取 |
| 连接模型 | 独占 subscribe 连接 | 普通连接，复用 |
| 去重 | `__server_id` | `__server_id`（相同逻辑） |
| 重启恢复 | 不支持 | `XPENDING` + 从 "0" 重放 |
| 消息裁剪 | 不需要 | `XTRIM MAXLEN ~ 10000` |
| 监控 | 无 | `XINFO STREAM/GROUPS/CONSUMERS` |
| 消息回溯 | 不支持 | `XREAD` 从任意 ID 开始 |


## 十五、预期效果

| 场景 | 当前 | 改造后 |
|------|------|--------|
| alice(VM1) → bob(VM2) 私聊 | ❌ user not online | ✅ 消息正常送达 |
| alice(VM1) 群聊 | VM2/VM3 看不到 | ✅ 所有 VM 用户收到 |
| VM2 崩溃重启 | — | ✅ 积压消息自动重放 |
| Redis 宕机 | 单机降级 | 降级为本地模式 |
| 查看消息积压 | 无 | `XPENDING` 查看 |
