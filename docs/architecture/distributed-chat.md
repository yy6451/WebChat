# 分布式聊天室系统架构设计

> **文档类型**：架构设计文档
> **状态**：设计方案 —— 大部分模块已实现，实际 API 与文档有细微差异
> **实际代码**：`backend/src/module/chat/` 下所有文件均已有实体实现
> **⚠️ 关键差异**：
> - 本文档设计中 `MessageDispatcher` 有 `dispatch()` + `onBusMessage()` 两个入口，实际实现中通过回调注入模式（`setDeliverCallback` / `setBroadcastCallback`）解耦 ChatRoom
> - 本文档设计中 `ChatMessageBus` 接收 `MessageCodec::ChatMessage`，实际实现中接收 `std::string`（已编码的 JSON 字符串）
> - 实际实现中 `database_init.sql` 的 `message_log` 表**不含** `msg_id` 字段（本文档设计中有，为未来预留）
> - `OnlineUserManager` 实际 API 方法名可能与设计文档有轻微差异（如 `refreshHeartbeat` → `refresh`）

> 基于 TinyWebServer，从单机聊天室升级为三节点分布式 WebSocket 聊天系统。

---

## 第一部分：整体架构图

```
                              Client (Browser / App)
                                    │
                         Nginx (可选负载均衡)
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
         ┌────▼────┐           ┌────▼────┐           ┌────▼────┐
         │   VM1    │           │   VM2    │           │   VM3    │
         │ :9007    │           │ :9007    │           │ :9007    │
         └────┬─────┘           └────┬─────┘           └────┬─────┘
              │                      │                      │
              │  ┌───────────────────┼───────────────────┐  │
              │  │             Redis Cluster              │  │
              │  │         (3 Master + 3 Replica)         │  │
              │  │                                       │  │
              │  │  ● Key-Value（用户密码、Token）       │  │
              │  │  ● Hash（用户会话状态）               │  │
              │  │  ● Set（好友关系、在线用户房间列表）  │  │
              │  │  ● SortedSet（活跃用户 TTL）          │  │
              │  │  ● INCR（消息序列号）                 │  │
              │  │  ● Streams（跨服务器消息总线）★关键   │  │
              │  └───────────────────────────────────────┘  │
              │                      │                      │
              └──────────────────────┼──────────────────────┘
                                     │
                              ┌──────▼──────┐
                              │    MySQL     │
                              │  (仅 VM1)   │
                              │             │
                              │  ● user     │
                              │  ● friend   │
                              │  ● message  │
                              │  ● request  │
                              └─────────────┘
```

### 模块分层

```
┌─────────────────────────────────────────────┐
│              应用层 (Application)            │
│  ChatService  ChatRoom  FriendService       │
│  AuthService  AuthManager  ChatStateStore   │
├─────────────────────────────────────────────┤
│            消息总线层 (Message Bus)           │
│  ChatMessageBus  OnlineUserManager          │
│  MessageDispatcher                          │
├─────────────────────────────────────────────┤
│              协议层 (Protocol)               │
│  HttpRequest  HttpResponse  WebSocketFrame  │
│  MessageCodec  ChatMessage                  │
├─────────────────────────────────────────────┤
│            基础设施层 (Infrastructure)        │
│  RedisClient  SQLConnectionPool             │
│  TimerWheel  ThreadPool  Buffer  Log        │
├─────────────────────────────────────────────┤
│              网络层 (Network)                │
│  WebServer  Connection  epoll               │
│  HttpConnection  WebSocketConnection        │
└─────────────────────────────────────────────┘
```


## 第二部分：各模块职责

### 2.1 不变模块（已有，无需大改）

| 模块 | 职责 | 改动程度 |
|------|------|---------|
| WebServer | epoll 事件循环、连接管理、定时器 | 最小改动（初始化新模块） |
| HttpConnection | HTTP 请求解析和响应 | 不变 |
| WebSocketConnection | WebSocket 帧收发 | 不变 |
| AuthManager | 用户认证、Token 管理 | 不变 |
| AuthService | 注册/登录 API | 不变 |
| FriendService | 好友关系 API | 不变 |
| ChatService | 消息业务处理 | 小改（接入 MessageBus） |
| ChatRoom | 房间管理和本地消息路由 | 中等改动（拆分职责） |
| ChatStateStore | Redis 聊天状态存储 | 不变 |
| RedisClient | Redis 命令封装 | 增加 Stream 命令 |
| TimerWheel | 时间轮定时器 | 不变 |

### 2.2 新增模块

| 模块 | 职责 | 设计原则 |
|------|------|---------|
| **ChatMessageBus** | 跨服务器消息总线，封装 Stream 操作 | 厚封装：对内收敛 Redis Stream 细节，对外暴露简单 publish + 回调 |
| **OnlineUserManager** | 全集群在线用户状态管理 | 轻量：仅维护 username→server_id 映射，不做业务逻辑 |
| **MessageDispatcher** | 消息路由决策：本地投递 vs 远程转发 | ChatRoom 委托其决策投递路径 |
| **MessageCodec** | 消息标准化编解码，统一 msg_id 生成 | 独立于业务，便于未来协议升级 |
| **OfflineMessageManager**（可选优化） | 离线消息持久化和重放 | 第一阶段不实现，预留接口 |

### 2.3 职责边界

```
ChatRoom          → 管理本机 WebSocket 连接，执行本地消息投递
ChatMessageBus    → 封装所有 Redis 交互，暴露 onMessage 回调
OnlineUserManager → 回答"xx 用户在哪个服务器"
MessageDispatcher → 决定消息走本地还是走总线
ChatService       → 业务处理入口，协调上述模块
```


## 第三部分：Redis 数据结构设计

### 3.1 已有结构（保持不变）

```
# 用户密码
user:pwd:{username}              STRING     bcrypt_hash

# Token
token:{token}                    STRING(TTL)  "userId|username|expireAt"

# 活跃用户（已登录，自动过期）
active_users                     ZSET        score=expireAt  member=username

# 好友请求
friend:requests:{username}       SET         {请求者列表}

# 好友关系
friend:relations:{username}      SET         {好友列表}

# 聊天室用户（保留给房间管理用）
chat:room:{roomId}:users         SET         {在线用户列表}
chat:room:{roomId}:seq           INCR        消息序列号
chat:user:{username}             HASH        userId/nickname/roomId/heartbeat
```

### 3.2 新增结构

```
# ★ 跨服务器消息 Stream
chat:messages                    STREAM      AOF 持久化，MAXLEN ~ 10000
  Consumer Group: group:server-{server_id}
  Consumer: consumer-1

# ★ 在线用户 → 所在服务器映射
online:server:{username}         STRING(TTL=70s)  "server_id"
  TTL 设为心跳超时的 1.2 倍，用户掉线时自动清除

# ★ 消息 ID 生成器
chat:msg:id                      INCR        全局唯一递增 ID
```

### 3.3 为什么这样设计

**`online:server:{username}` 用 STRING + TTL**：
- 心跳包每 30s 刷新一次 TTL → 70s TTL 给冗余
- 用户异常断线 → 70s 后 Key 自动过期 → OnlineUserManager 自动感知下线
- 无需额外的「在线状态清除」定时任务

**不用 Pub/Sub 而是在线状态用 Key-Value**：
- Pub/Sub 消息不持久化，服务器重启后不知道谁在线
- Key-Value + TTL 天然支持「重启后从 Redis 重建在线状态」


## 第四部分：MySQL 表设计

```sql
-- 用户表（已有）
CREATE TABLE IF NOT EXISTS user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    passwd VARCHAR(255) NOT NULL,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友关系表（已有）
CREATE TABLE IF NOT EXISTS friend_relation (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    friend_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_friend(user_id, friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友请求表（已有）
CREATE TABLE IF NOT EXISTS friend_request (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT NOT NULL,
    to_user_id INT NOT NULL,
    status TINYINT NOT NULL DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_friend_req(from_user_id, to_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ★ 消息日志表（已有）
CREATE TABLE IF NOT EXISTS message_log (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT NOT NULL,
    to_user_id INT NOT NULL,
    msg_type VARCHAR(16) NOT NULL COMMENT 'chat/private/system',
    content TEXT NOT NULL,
    is_read TINYINT NOT NULL DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_to_user_read(to_user_id, is_read)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='消息记录表';
```


## 第五部分：消息流转流程

### 5.1 私聊：alice@VM1 → bob@VM2

```
alice(VM1) 发送私聊 "hello" 给 bob:

VM1:
  ChatService::processJsonMessage(conn, json)
    │
    ▼
  ChatRoom::SendToUser("bob", message)
    │
    ├─ 本地 user_index_.find("bob") → 未找到
    │
    ▼
  OnlineUserManager::whereIs("bob")
    → GET online:server:{bob} → "192.168.118.133:9007" → bob 在 VM2
    │
    ▼
  MessageDispatcher::route(message, target_id="bob")
    → 目标不在本机 → 走总线
    │
    ▼
  ChatMessageBus::publish("chat:messages", message)
    → XADD chat:messages * data {...} target_server "192.168.118.133:9007"
    │
    ▼
  写入 MySQL message_log（异步完成）

VM2:
  ChatMessageBus::consumeLoop()
    → XREADGROUP BLOCK 1000 → 收到消息
    │
    ▼
  MessageDispatcher::onBusMessage(msg)
    → 检查 target_server == my_server_id? YES → 本地投递
    │
    ▼
  ChatRoom::deliverLocal("bob", message)
    → user_index_.find("bob") → sendData() ✅
    │
    ▼
  XACK 确认消费
```

### 5.2 群聊：alice@VM1 → 所有服务器群聊

```
alice(VM1) 群聊 "hello":

VM1:
  ChatRoom::Broadcast(message)
    ├─ 本机 connections_ 遍历 → sendData()（VM1 用户立即收到）
    └─ ChatMessageBus::publish("chat:messages", message)
       XADD chat:messages * data {...} target_role "broadcast"

VM2 & VM3:
  consumeLoop() → 收到消息
    → target_role == "broadcast" → 本机全量广播
    → connections_ 遍历 → sendData() ✅
```

### 5.3 群聊去重策略

```
消息加入 server_id 字段：
{ "msg_id":"1718000000-uuid-0001", "server_id":"192.168.118.131", ... }

VM1 的 consumeLoop 也收到自己发的消息：
  → MessageDispatcher: server_id == my_server_id → 跳过（已本地投递过）
```

---

## 第六部分：需要新增的类

### 6.1 MessageCodec（消息编解码器）

```cpp
// module/chat/message_codec.h
class MessageCodec {
public:
    struct ChatMessage {
        std::string msg_id;        // 全局唯一 ID
        std::string server_id;     // 发送服务器 ID
        std::string type;          // chat/private/system
        std::string from;          // 发送者
        std::string to;            // 接收者
        std::string content;       // 消息内容
        int64_t timestamp;
        uint64_t seq;
        std::string target_server; // 目标服务器（私聊路由用）
        std::string target_role;   // "broadcast" / "unicast"
    };

    // JSON ↔ ChatMessage 双向转换
    static std::string encode(const ChatMessage& msg);
    static ChatMessage decode(const std::string& json);

    // 生成全局唯一 ID
    static std::string generateMsgId(const std::string& serverId);
};
```

**面试价值**：体现了「协议层独立于业务层」的设计思想，后续切换到 Protobuf 或接入 Kafka 时只需改这一个类。

### 6.2 OnlineUserManager（在线用户管理器）

```cpp
// module/chat/online_user_manager.h
class OnlineUserManager {
public:
    static OnlineUserManager* instance();

    // 用户上线：记录 username → server_id
    void onUserOnline(const std::string& username, const std::string& serverId);

    // 用户下线：主动清除（优雅离开时调用）
    void onUserOffline(const std::string& username);

    // 查询用户所在服务器
    std::string whereIs(const std::string& username);

    // 刷新心跳（WebSocket 心跳调起）
    void refreshHeartbeat(const std::string& username, const std::string& serverId);

    // 获取全集群在线用户列表
    std::vector<std::string> allOnlineUsers();

    // 初始化 Redis 连接
    bool initRedis(const std::string& host, int port,
                   const std::string& password, std::string* err);

private:
    RedisClient redis_;
    std::string server_id_;
};
```

**为什么需要它**：
- ChatRoom 只能看到本机连接，不知道 bob 在 VM2
- 需要一个「全集群在线状态字典」
- Redis STRING + TTL 实现，零维护成本

### 6.3 MessageDispatcher（消息路由决策器）

```cpp
// module/chat/message_dispatcher.h
class MessageDispatcher {
public:
    static MessageDispatcher* instance();

    // 发送消息入口：决定本地投递还是走总线
    // 返回 true 表示消息已被处理（本地或远程）
    bool dispatch(const MessageCodec::ChatMessage& msg);

    // 收到总线消息的回调：本地投递
    void onBusMessage(const MessageCodec::ChatMessage& msg);

    void setServerId(const std::string& id) { server_id_ = id; }

private:
    bool deliverLocal(const MessageCodec::ChatMessage& msg);

    std::string server_id_;
};
```

**为什么需要它**：
- 把「消息去哪」的决策逻辑从 ChatRoom 中抽离
- ChatRoom 只负责「本机的连接集合」，不关心消息路由
- 后续加了 Gateway 或 Kafka，改造集中在一个类

### 6.4 ChatMessageBus（跨服务器消息总线）

```cpp
// module/chat/chat_message_bus.h
class ChatMessageBus {
public:
    static ChatMessageBus* instance();

    bool init(const std::string& host, int port,
              const std::string& password, const std::string& serverId,
              std::string* err);

    // 发布消息到 Stream
    void publish(const MessageCodec::ChatMessage& msg);

    // 优雅关闭
    void shutdown();

    const std::string& serverId() const { return server_id_; }

private:
    void consumeLoop();     // XREADGROUP 循环
    void ackMessage(const std::string& msgId);

    std::string server_id_;
    std::string consumer_group_;
    std::string consumer_name_;
    RedisClient redis_;
    std::thread consume_thread_;
    std::atomic<bool> running_{false};
};
```

### 6.5 最终模块关系图

```
ChatService (业务入口)
    │
    ├──> MessageDispatcher (路由决策)
    │         ├──> 本地:  ChatRoom::deliverLocal()
    │         └──> 远程:  ChatMessageBus::publish()
    │                         └──> XADD chat:messages
    │
    ├──> ChatRoom (连接管理 + 本地投递)
    │         ├──> connections_ (本机 WebSocket 集合)
    │         └──> user_index_ (username → conn)
    │
    ├──> OnlineUserManager (全集群在线状态)
    │         └──> Redis online:server:{username}
    │
    └──> ChatMessageBus (消费线程)
              └──> XREADGROUP → MessageDispatcher::onBusMessage()
```


## 第七部分：具体代码改造清单

### 7.1 RedisClient（新增 6 个方法）
**文件**：`backend/src/utils/redis/redis_client.h/.cpp`

```
xadd(key, id, fields, out_id, err)       → XADD
xgroup_create(key, group, id, mkstream, err) → XGROUP CREATE
xreadgroup(group, consumer, key, id, count, block, result, err) → XREADGROUP
xack(key, group, id, err)               → XACK
xtrim_maxlen(key, maxlen, err)          → XTRIM MAXLEN ~
xpending_count(key, group, err)         → XPENDING
```

### 7.2 ChatRoom（拆分职责）
**文件**：`backend/src/module/chat/chat_room.h/.cpp`

```diff
- void Broadcast(const std::string& message, WebSocketConnection* exclude);
+ void Broadcast(const std::string& message, WebSocketConnection* exclude);
  // 实现中去掉 ChatMessageBus::publish() 调用，
  // 转由 MessageDispatcher 负责跨服务器部分

+ void deliverLocal(const std::string& targetUser, const std::string& message);
- bool SendToUser(const std::string& username, const std::string& message);
+ bool SendToUser(const std::string& username, const std::string& message);
  // 实现中不再调用 ChatMessageBus，转由 MessageDispatcher 决策
```

### 7.3 ChatService（接入 MessageDispatcher）
**文件**：`backend/src/module/chat/chat_service.cpp`

```diff
// processJsonMessage() 中的广播调用
- ChatRoom::instance()->Broadcast(message, nullptr);
+ MessageDispatcher::instance()->dispatch(msg);

// 私聊调用
- ChatRoom::instance()->SendToUser(to, message);
+ MessageDispatcher::instance()->dispatch(msg);
```

### 7.4 WebServer（初始化新模块）
**文件**：`backend/src/core/webserver.cpp`

```diff
void WebServer::eventListen() {
    // ...existing code...

+   // 初始化跨服务器消息总线
+   std::string serverId = config.server_announce_ip + ":" + std::to_string(m_port);
+
+   OnlineUserManager::instance()->initRedis(redis_host, redis_port, redis_pass, &err);
+   OnlineUserManager::instance()->setServerId(serverId);
+
+   MessageDispatcher::instance()->setServerId(serverId);
+
+   ChatMessageBus::instance()->init(redis_host, redis_port, redis_pass, serverId, &err);
}
```

### 7.5 ChatRoom::Join（标记在线状态）
**文件**：`backend/src/module/chat/chat_room.cpp`

```diff
void ChatRoom::Join(WebSocketConnection* conn) {
+   // 标记全集群在线状态
+   OnlineUserManager::instance()->onUserOnline(username, server_id_);
    // ...existing join logic...
}
```

### 7.6 ChatRoom::Leave（标记离线）
```diff
void ChatRoom::Leave(WebSocketConnection* conn) {
+   OnlineUserManager::instance()->onUserOffline(username);
    // ...existing leave logic...
}
```

### 7.7 ChatService::onHeartbeat（刷新在线状态）
```diff
void ChatService::onHeartbeat(WebSocketConnection* conn) {
    // ...existing...
+   OnlineUserManager::instance()->refreshHeartbeat(user, server_id_);
}
```

### 7.8 config.cpp 新增 -I 参数
```diff
+ std::string server_announce_ip;

  case 'I': {
+     server_announce_ip = optarg;
      break;
  }
```

### 7.9 CMakeLists.txt
```diff
+ src/module/chat/chat_message_bus.cpp
+ src/module/chat/message_dispatcher.cpp
+ src/module/chat/online_user_manager.cpp
+ src/module/chat/message_codec.cpp
```


## 第八部分：改造路线图（RoadMap）

### 第一阶段：基础设施就绪（已完成 ✅）
- [x] Redis Cluster 三节点部署
- [x] MySQL 双写架构
- [x] TimerWheel 时间轮优化
- [x] database_init.sql 统一建表

### 第二阶段：跨服务器通信（当前）
- [ ] RedisClient 增加 Stream 命令封装
- [ ] MessageCodec 消息编解码器
- [ ] ChatMessageBus 消费总线（XREADGROUP 循环线程）
- [ ] ChatRoom 本地投递 + MessageDispatcher 路由
- [ ] 验证：alice@VM1 → bob@VM2 私聊

### 第三阶段：在线状态系统
- [ ] OnlineUserManager 实现
- [ ] Redis online:server:{username} STRING + TTL
- [ ] ChatRoom::Join/Leave 集成
- [ ] ChatService 心跳刷新
- [ ] 验证：Cross-server 在线用户列表正确

### 第四阶段：消息可靠性（可选优化）
- [ ] 消息 ACK 机制（已由 XACK 提供）
- [ ] 崩溃恢复：XPENDING 重放未确认消息
- [ ] Stream 消息裁剪策略微调
- [ ] message_log 批量写入优化

### 第五阶段：生产级增强（可选优化）
- [ ] Nginx TCP 负载均衡 9007 端口
- [ ] 消息压缩（超过 512B 时 gzip）
- [ ] 连接速率限制（令牌桶）
- [ ] Prometheus 指标暴露

### 第六阶段：高阶扩展（未来）
- [ ] Gateway 层：独立 WS Gateway + Chat Server 分离
- [ ] Kafka 替换 Redis Streams（十万级在线）
- [ ] 消息推送（APNs/FCM）


## 第九部分：架构评审

### 9.1 与主流 IM 架构对比

| 维度 | Discord | 本项目设计 | 是否合理 |
|------|---------|-----------|---------|
| 消息总线 | 自研消息队列 | Redis Streams | ✅ 同级别（Medium 规模） |
| 在线状态 | Ringpop (Hash Ring) | Redis STRING + TTL | ✅ 更简单但够用 |
| 持久化 | Cassandra | MySQL | ✅ 规模适配 |
| 路由 | 有状态 Gateway | OnlineUserManager 查表 | ✅ 核心逻辑一致 |
| 连接层 | Elixir + WebSocket | C++ epoll + WebSocket | ✅ 高性能 |

### 9.2 设计亮点（面试可讲）

1. **Redis 职责精准**：不是「什么都用 Redis」，而是 Key-Value 做缓存、Set 做好友关系、ZSET 做 TTL、Streams 做消息总线——**每种数据结构都有明确理由**。

2. **OnlineUserManager 设计**：STRING + TTL 代替 Pub/Sub 做在线状态，天然支持「服务器崩溃后自动清理」，比 Pub/Sub 的「发事件→其他服务器响应」方案更健壮。

3. **MessageDispatcher 路由决策层**：把「本地投递 vs 远程转发」抽象为一个独立层，ChatRoom 不用感知消息是本地还是远程的。后续加 Gateway 或 Kafka 只需要改这一个类。

4. **MessageCodec 协议层隔离**：消息序列化/反序列化独立于业务逻辑，后续升级到 Protobuf 或 MessagePack 零业务改动。

5. **渐进式改造**：每个阶段都有独立可验证的成果，不会出现「改了一大堆结果跑不起来」。

### 9.3 可能被面试官追问的点 & 如何应对

**Q: 为什么不用 Kafka？**
> A: 项目目前是三节点、百级在线。Kafka 的运维复杂度（ZooKeeper、分区、消费组管理）远超 Redis Streams。Redis 已经在技术栈中，零新依赖。如果未来在线数增长到十万级，Streams → Kafka 的迁移成本可控——因为 MessageBus 接口已经抽象好了。

**Q: 在线状态用 Redis STRING + TTL，用户断线后 70 秒才对其他人可见，会不会太慢？**
> A: TTL 可以调为 30s（等于心跳间隔），用 70s 是给网络抖动的容错。可以加一个「优雅断开」路径：WebSocket CLOSE 时主动 DEL online:server:{username}，立即下线。TTL 只兜底异常断线。

**Q: 消费者不 ACK 会怎样？**
> A: XPENDING 会持续增长。生产环境需要加监控：`XPENDING count > 100 → 告警`。消费线程加 try-catch 保证不崩溃，XACK 在 try 块末端执行。

**Q: 如果一条消息需要发给 1000 个在线用户怎么办？**
> A: 群聊消息发给每个服务器只需要一条 XADD。每个服务器收到后在本地遍历 connections_ 发送——1000 个用户分布在 3 台服务器上，每台只要遍历 ~333 个连接，`sendData()` 是内存操作，微秒级完成。

### 9.4 不适合本阶段的设计（明确不做）

| 设计 | 为什么不加 |
|------|-----------|
| Gateway 层 | 当前 3 节点不需要，加了反而增加调试复杂度 |
| 消息队列（Kafka） | Redis Streams 已足够 |
| 分布式 ID 生成器（Snowflake） | Redis INCR 够用，Snowflake 增加运维负担 |
| Service Mesh（Istio） | 3 节点用不上 |
| 读写分离 MySQL | 单点 MySQL 够用，加主从不是本期目标 |

### 9.5 总结

> 这个架构的核心思想：**Redis 做热数据，MySQL 做冷数据，MessageBus 做桥**。不堆砌技术，每个组件都有明确的「它解决了什么问题」。在面试中，面试官最看重的是「为什么选这个方案，而不是另一个」——这也是本文档花最多篇幅解释的部分。
