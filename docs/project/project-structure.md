# WebChat 项目结构（前后端分离版）

> C++17 后端 + Vue3 前端的分布式 WebSocket 聊天系统。

---

## 1. 项目整体结构

```
WebChat/
├── backend/                    # 后端 C++ 工程
│   ├── CMakeLists.txt          # 后端构建入口
│   ├── Dockerfile              # Docker 部署
│   ├── scripts/                # 部署/运维脚本
│   │   ├── run_redis.sh        # Redis 集群/单机启动
│   │   ├── cluster_create.sh   # Redis Cluster 初始化
│   │   ├── cluster_deploy_node.sh  # 单机双实例部署
│   │   ├── nginx_setup.sh      # Nginx 代理配置
│   │   ├── mysql_seed_demo.sh  # MySQL 造数脚本
│   │   ├── mysql_unread_demo.sh    # 未读消息测试
│   │   └── cleanup_logs.sh     # 日志清理
│   └── src/                    # 后端源码（详见下方分层）
├── frontend/                   # 前端 Vue3 工程
│   ├── package.json
│   ├── vite.config.ts
│   ├── tsconfig.json
│   ├── index.html
│   ├── public/
│   └── src/
│       ├── main.ts
│       ├── App.vue
│       ├── api/                # HTTP/WebSocket 封装
│       ├── assets/
│       ├── components/         # UI 组件
│       ├── composables/
│       ├── router/
│       ├── store/              # Pinia 状态管理
│       ├── types/              # TypeScript 类型
│       └── views/              # 页面视图
├── docs/                       # 文档
├── build/                      # CMake 构建输出
├── logs/                       # 运行日志
├── test_pressure/              # 压力测试工具
│   └── webbench-1.5/
├── tools/                      # 测试/工具脚本
├── archive/                    # 旧源码与历史文档归档
├── CMakeLists.txt              # 根 CMake（聚合 backend）
├── database_init.sql           # MySQL 表初始化
└── README.md
```

## 2. 后端分层架构

源码位于 `backend/src/`，按职责分 7 个模块：

| 模块 | 路径 | 职责 |
|------|------|------|
| **入口** | `main.cpp` | 配置解析、模块初始化、服务器启动 |
| **配置** | `config/` | 命令行参数解析（`Config` 类） |
| **核心调度** | `core/` | epoll 事件循环、连接管理、消息总线初始化 |
| **连接层** | `core/connection/` | HTTP/WebSocket 连接处理（`HttpConnection`, `WebSocketConnection`） |
| **协议层** | `protocol/` | HTTP 请求解析 + WebSocket 帧编解码 |
| **业务模块** | `module/http/` | REST API（注册/登录/好友）+ 路由 |
| **聊天模块** | `module/chat/` | 聊天室、消息总线、在线状态管理 |
| **工具库** | `utils/` | 日志、线程池、Redis 客户端、MySQL 连接池、定时器、认证 |

### 2.1 聊天模块详解

| 类 | 文件 | 职责 |
|----|------|------|
| `ChatRoom` | `chat_room.h/.cpp` | 本机 WebSocket 连接管理、本地消息投递 |
| `ChatService` | `chat_service.h/.cpp` | 聊天业务入口，协调消息路由 |
| `ChatStateStore` | `chat_state_store.h/.cpp` | Redis 聊天状态读写 |
| `MessageCodec` | `message_codec.h/.cpp` | 消息 JSON 编解码、msg_id 生成 |
| `MessageDispatcher` | `message_dispatcher.h/.cpp` | 消息路由决策（本地 vs 远程） |
| `ChatMessageBus` | `chat_message_bus.h/.cpp` | Redis Streams 跨服务器消息总线 |
| `OnlineUserManager` | `online_user_manager.h/.cpp` | 全集群在线用户状态管理 |

## 3. 前端分层

- `views/`：页面（登录/注册/聊天）。
- `components/`：可复用 UI 组件（好友列表、消息列表、聊天输入）。
- `api/`：HTTP 与 WebSocket 封装。
- `store/`：Pinia 鉴权态与会话态管理。
- `types/`：TypeScript 协议与接口类型定义。
- `composables/`：组合式 API（WebSocket、认证等）。

## 4. 文件命名规范

所有文件名已统一为 **snake_case**（小写+下划线）：

- 头文件: `.h`
- 实现文件: `.cpp`
- 目录名: 小写

示例: `http_connection.h`, `websocket_codec.cpp`, `chat_message_bus.h`

## 5. 前后端集成

### 5.1 后端配置
- **静态文件目录**: `frontend/dist`（Vite 构建产物）
- **WebSocket 端点**: `/chat?token=<token>`
- **监听端口**: `9007`（可通过 `-p` 修改）
- **HTTP API 前缀**: `/api/`

### 5.2 前端构建
```bash
cd frontend
npm install
npm run build          # 输出到 frontend/dist/
```

### 5.3 API 端点一览

| 方法 | 路径 | 说明 | 认证 |
|------|------|------|:---:|
| POST | `/api/register` | 注册 | - |
| POST | `/api/login` | 登录返回 token | - |
| GET | `/api/userinfo` | 当前用户信息 | Bearer |
| GET | `/api/friends` | 好友列表（含未读数） | Bearer |
| POST | `/api/addfriend` | 发送好友请求 | Bearer |
| GET | `/api/friendrequests` | 待处理好友请求 | Bearer |
| POST | `/api/verifyfriend` | 接受/拒绝好友请求 | Bearer |
| GET | `/api/searchuser` | 搜索用户 | Bearer |
| POST | `/api/readfriend` | 标记好友消息已读 | Bearer |

## 6. 构建与运行

### 6.1 编译
```bash
cd ~/TinyWebServer
cmake -S . -B build
cmake --build build -j
# 产物: build/backend/server
```

### 6.2 启动
```bash
# 纯 Redis 模式
./build/backend/server -p 9007 -d 0 -H 60

# MySQL 双写模式
./build/backend/server -p 9007 -d 1 -H 60 -M localhost

# 跨服模式（指定本机对外 IP）
./build/backend/server -p 9007 -d 1 -H 60 -R 127.0.0.1 -P 7000 -I 192.168.118.131
```

### 6.3 启动前端开发服务器
```bash
cd frontend && npm run dev
# 浏览器访问 http://localhost:5173
```

## 7. 迭代完成项

1. `backend/src` 已实体化，去除链接映射。
2. 已接入 MySQL 好友关系持久化、私聊消息未读统计接口。
3. 前端已支持好友未读数渲染与会话切换自动清零。
4. 已提供 MySQL unread 闭环脚本：`backend/scripts/mysql_seed_demo.sh`、`backend/scripts/mysql_unread_demo.sh`。
5. **Redis Cluster**：三节点 6 实例集群部署方案（3 Master + 3 Replica 交叉部署）。
6. **跨服务器聊天**：基于 Redis Streams + Consumer Groups 的消息总线，支持私聊精准路由和群聊全集群广播。
7. **Nginx 代理**：TCP Stream 代理统一入口，支持 least_conn / ip_hash 负载均衡。

## 8. 历史记录

以下命名/路径变更已完成（历史记录，仅供参考）：

- 文件名大写改小写（`HttpConnection.h` → `http_connection.h`）
- 旧 `resources/` 目录 → `frontend/dist`
- 旧端口 `9006` → `9007`
- 旧 URL 风格（`2Register`, `3Login`）→ RESTful（`/api/register`, `/api/login`）
- 旧前端架构（静态 HTML）→ Vue3 + Vite + TypeScript

旧源码已归档至 `archive/legacy_archive_20260324_022258.tar.gz`。
