#ifndef MODULE_CHAT_ONLINE_USER_MANAGER_H
#define MODULE_CHAT_ONLINE_USER_MANAGER_H

#include <string>
#include <vector>
#include "../../utils/redis/redis_client.h"

// 全集群在线用户状态管理器。
//
// 超时机制（两套，各司其职）:
//   主力：时间轮（90s）     — 监控 TCP 连接，超时 → Close() → onUserOffline() → DEL
//   兜底：Redis TTL（100s） — 仅处理服务器进程 crash 场景（时间轮代码无法执行时
//                             由 Redis 自动清理残留 key，防止跨服路由到死服务器）
//
// TTL > 时间轮超时，保证正常情况下 TTL 不会早于时间轮触发。
//
// 为什么用 STRING + TTL 而不是 Pub/Sub:
//   - Pub/Sub 消息不持久化，服务器重启后不知道谁在线
//   - STRING + TTL 天然支持「重启后从 Redis 重建在线状态」

class OnlineUserManager {
public:
    static OnlineUserManager* instance();

    OnlineUserManager(const OnlineUserManager&) = delete;
    OnlineUserManager& operator=(const OnlineUserManager&) = delete;

    // 初始化 Redis 连接
    bool initRedis(const std::string& host, int port,
                   const std::string& password, std::string* err);

    // 设置本服务器 ID
    void setServerId(const std::string& id) { server_id_ = id; }

    // 用户上线：记录 username → server_id
    void onUserOnline(const std::string& username);

    // 用户下线：主动清除（优雅离开时调用）
    void onUserOffline(const std::string& username);

    // 查询用户所在服务器 ID，不在线返回空串
    std::string whereIs(const std::string& username);

    // 刷新心跳 TTL（WebSocket 心跳调起）
    void refreshHeartbeat(const std::string& username);

    // 获取指定房间的下一个消息序列号（INCR）
    uint64_t nextSeq(const std::string& roomId);

    // 获取全集群在线用户列表（从 Redis SET 聚合）
    std::vector<std::string> allOnlineUsers();

private:
    OnlineUserManager();
    ~OnlineUserManager() = default;

    static const int kOnlineTTL = 100;  // 心跳间隔(30s) * 3 + 冗余(10s)

    std::string onlineKey(const std::string& username) {
        return "online:server:{" + username + "}";
    }

    std::string seqKey(const std::string& roomId) {
        return "chat:room:{" + roomId + "}:seq";
    }

    // 全集群在线用户 SET（{all} hash tag 固定到同一 slot）
    std::string allOnlineKey() {
        return "online:users:{all}";
    }

    RedisClient redis_;
    bool redis_ready_;
    std::string server_id_;
};

#endif
