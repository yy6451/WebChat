/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/auth_manager.h
 * 类型: Header
 * 作用: Token 生命周期管理（签发/校验/吊销）+ Redis 连接。
 *       同时提供用户/好友数据的 Redis 便捷操作方法，供 service 层在 -d 0/-d 1 双路径下复用。
 * 关键关注点:
 * 1) Token 的生成、校验、删除均在 Redis 中完成，TTL 自动过期。
 * 2) 用户密码和好友关系的方法是对 Redis 操作的薄封装，供 auth_service / friend_service 调用。
 * 3) MySQL→Redis 双写同步方法由各 service 在 -d 1 路径中调用，保持缓存一致。
 * 维护建议:
 * 1) 修改接口时同步检查调用方与单元/集成测试。
 * 2) 修改并发逻辑时优先保证可见性与无竞态。
 * 3) 修改协议字段时保持前后端兼容与错误码稳定。
 */
#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <string>                     // std::string
#include <vector>                     // std::vector
#include <chrono>                     // std::chrono::system_clock 和 std::chrono::seconds
#include "../redis/redis_client.h"

// 用户登录态信息结构体
struct UserInfo {
    int id;                                    // 用户 ID
    std::string username;                      // 用户名
    std::chrono::system_clock::time_point expire_time; // 令牌过期时间点

    UserInfo() : id(0) {}

    // 构造函数：使用用户 ID、用户名和过期时长创建登录态
    UserInfo(int uid, const std::string& name, std::chrono::seconds duration)
        : id(uid), username(name) {
        expire_time = std::chrono::system_clock::now() + duration;
    }

    // 检查当前时间是否超过 expire_time
    bool isExpired() const {
        return std::chrono::system_clock::now() > expire_time;
    }
};

class AuthManager {
public:
    // 全局单例访问入口
    static AuthManager* instance();

    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    // ========== Token 管理（核心职责）==========

    // 签发 token，写入 Redis（TTL 自动过期）
    std::string generateToken(int userId, const std::string& username);

    // 校验 token 是否有效，成功时填充 outInfo
    bool validateToken(const std::string& token, UserInfo& outInfo);

    // 删除指定 token（登出或强制失效）
    void removeToken(const std::string& token);

    // 初始化 Redis Cluster 连接
    bool initRedis(const std::string& host,
                   int port,
                   const std::string& password,
                   int db,
                   std::string* err);

    // 返回 Redis 是否就绪
    bool isRedisReady() const;

    // ========== 用户密码（Redis 便捷封装，供 auth_service 调用）==========

    bool registerUser(const std::string& username, const std::string& password, std::string& err);
    bool loginUser(const std::string& username, const std::string& password, std::string& err);
    bool hasKnownUsername(const std::string& username);

    // ========== 好友关系（Redis 便捷封装，供 friend_service 调用）==========

    bool sendFriendRequest(const std::string& fromUser, const std::string& toUser, std::string& err);
    std::vector<std::string> listPendingRequests(const std::string& toUser);
    bool verifyFriendRequest(const std::string& toUser, const std::string& fromUser, bool accept, std::string& err);
    std::vector<std::string> listFriends(const std::string& username);
    bool areFriends(const std::string& a, const std::string& b);

    // ========== MySQL→Redis 双写同步（供各 service 在 -d 1 路径调用）==========

    bool syncUserPassword(const std::string& username, const std::string& password, std::string& err);
    bool syncFriendRelation(const std::string& userA, const std::string& userB, std::string& err);
    bool syncFriendRequest(const std::string& from, const std::string& to, std::string& err);
    void removeFriendRequest(const std::string& toUser, const std::string& fromUser);

private:
    AuthManager();
    ~AuthManager() = default;

    std::string generateRandomString(size_t length);
    std::string base64Encode(const std::string& input);
    std::string base64Decode(const std::string& input);
    std::string hmacSha256(const std::string& key, const std::string& data);
    bool ensureRedis(std::string* err) const;

    RedisClient redis_;
    bool redis_ready_;
    std::string secret_key_;
    std::chrono::seconds token_duration_;
};

#endif // AUTH_MANAGER_H
