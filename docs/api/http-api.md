# HTTP API 文档

Base URL: `http://<host>:9007`

## 1) 注册

- `POST /api/register`
- `Content-Type: application/x-www-form-urlencoded`
- 参数：`username`, `password`, `repassword`

```bash
curl -X POST http://127.0.0.1:9007/api/register \
  -d "username=u1&password=123456&repassword=123456"
```

## 2) 登录

- `POST /api/login`
- 返回 token

```bash
curl -X POST http://127.0.0.1:9007/api/login \
  -d "username=u1&password=123456"
```

## 3) 当前用户信息

- `GET /api/userinfo`
- Header: `Authorization: Bearer <token>`

```bash
curl http://127.0.0.1:9007/api/userinfo \
  -H "Authorization: Bearer <token>"
```

## 4) 好友列表

- `GET /api/friends`
- Header: `Authorization: Bearer <token>`

返回结构（数组对象）：

```json
{
  "code": 0,
  "msg": "ok",
  "data": [
    { "username": "bob", "unread": 3 }
  ]
}
```

```bash
curl http://127.0.0.1:9007/api/friends \
  -H "Authorization: Bearer <token>"
```

## 5) 搜索用户

- `GET /api/searchuser`
- Header: `Authorization: Bearer <token>`
- 参数：`username`（URL 查询参数）

```bash
curl "http://127.0.0.1:9007/api/searchuser?username=bob" \
  -H "Authorization: Bearer <token>"
```

## 6) 添加好友

- `POST /api/addfriend`
- Header: `Authorization: Bearer <token>`
- 参数：`friend`

```bash
curl -X POST http://127.0.0.1:9007/api/addfriend \
  -H "Authorization: Bearer <token>" \
  -d "friend=u2"
```

说明：

- 好友关系由 Redis 维护。
- 若启用 MySQL（`-d 1`），历史消息/未读统计会落库。

## 7) 好友请求列表

- `GET /api/friendrequests`
- Header: `Authorization: Bearer <token>`

返回待处理的好友请求来源用户列表：

```json
{ "code": 0, "msg": "ok", "data": ["alice"] }
```

```bash
curl http://127.0.0.1:9007/api/friendrequests \
  -H "Authorization: Bearer <token>"
```

## 8) 处理好友请求

- `POST /api/verifyfriend`
- Header: `Authorization: Bearer <token>`
- 参数：`friend`, `action`（`accept` 接受 / `reject` 拒绝）

```bash
curl -X POST http://127.0.0.1:9007/api/verifyfriend \
  -H "Authorization: Bearer <token>" \
  -d "friend=alice&action=accept"
```

## 9) 会话已读回写

- `POST /api/readfriend`
- Header: `Authorization: Bearer <token>`
- 参数：`friend`

```bash
curl -X POST http://127.0.0.1:9007/api/readfriend \
  -H "Authorization: Bearer <token>" \
  -d "friend=bob"
```

说明：

- 启用 MySQL（`-d 1`）时会将该好友发来的未读私聊批量标记为已读。
- 未启用 MySQL（`-d 0`）时仅返回成功确认，前端本地清零未读计数。

## 10) MySQL unread 闭环脚本

```bash
cd /home/yy1/TinyWebServer
./backend/scripts/mysql_seed_demo.sh
./backend/scripts/mysql_unread_demo.sh
```

`mysql_unread_demo.sh` 会自动完成：

1. 登录获取 token
2. 调 `/api/friends` 查看未读（预期 > 0）
3. 调 `/api/readfriend` 标记已读
4. 再次调 `/api/friends` 验证未读归零
