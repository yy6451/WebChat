# WebSocket 协议文档

连接地址：

- `ws://<host>:9007/chat?token=<token>`
- 或开发模式：`ws://<host>:9007/chat?user=<username>`

## 消息格式（JSON）

```json
{
  "type": "chat|private|heartbeat|system|online_list",
  "from": "alice",
  "to": "room_default|bob",
  "content": "hello",
  "timestamp": 1710000000,
  "seq": 1001
}
```

## 语义

- `chat`: 群聊，`to=room_default`
- `private`: 私聊，`to=<username>`
- `heartbeat`: 心跳
- `system`: 系统通知
- `online_list`: 在线用户列表（服务端推送）

## 心跳

心跳由服务端主动驱动：

- 服务端每 5 个定时器 tick（约 25 秒）向所有 WebSocket 连接发送**协议层 PING 帧**。
- 浏览器 WebSocket 协议栈会自动回复 PONG，应用层无需处理。
- 连接失活时由心跳超时定时器（`-H`，默认 90 秒）负责清理。
