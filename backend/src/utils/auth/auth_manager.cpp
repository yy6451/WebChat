/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/auth_manager.cpp
 * 类型: Source
 * 作用: 鉴权模块：负责用户认证、令牌管理与登录态校验。
 * 关键关注点:
 * 1) 对外接口/类声明（或实现入口）与调用约束。
 * 2) 资源管理（内存、fd、锁、数据库连接）与异常路径回收。
 * 3) 并发与线程安全语义（线程池、锁、回调执行上下文）。
 * 4) 与其他模块的数据流边界（协议层/业务层/基础设施层）。
 * 维护建议:
 * 1) 修改接口时同步检查调用方与单元/集成测试。
 * 2) 修改并发逻辑时优先保证可见性与无竞态。
 * 3) 修改协议字段时保持前后端兼容与错误码稳定。
 */
#include "auth_manager.h"
#include "../log/log.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <limits>
#include <ctime>
#include <cstdlib>
#include <mysql/mysql.h>

// AuthManager 的实现文件，主要负责：
// 1. Token 生成与验证。
// 2. 用户注册/登录逻辑。
// 3. 好友关系与好友请求的 Redis 存储。
// 4. Redis Cluster 初始化与运行时状态检查。

// Base64 编码字符集，用于 base64Encode/base64Decode
const std::string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

namespace {
std::string tokenKey(const std::string& token) {
    return "token:{" + token + "}";
}

std::string userPwdKey(const std::string& username) {
    return "user:pwd:{" + username + "}";
}

std::string friendReqKey(const std::string& username) {
    return "friend:requests:{" + username + "}";
}

std::string friendRelKey(const std::string& username) {
    return "friend:relations:{" + username + "}";
}

bool splitTokenValue(const std::string& value, int* userId, std::string* username, long long* expireAt) {
    size_t p1 = value.find('|');
    if (p1 == std::string::npos) return false;
    size_t p2 = value.find('|', p1 + 1);
    if (p2 == std::string::npos) return false;
    if (userId) {
        *userId = std::atoi(value.substr(0, p1).c_str());
    }
    if (username) {
        *username = value.substr(p1 + 1, p2 - p1 - 1);
    }
    if (expireAt) {
        *expireAt = std::atoll(value.substr(p2 + 1).c_str());
    }
    return true;
}
}

// 单例访问器：首次调用时创建静态 manager，后续返回同一实例
AuthManager* AuthManager::instance() {
    static AuthManager manager;
    return &manager;
}

// 私有构造函数：初始化默认 token 有效期并加载持久化状态
AuthManager::AuthManager() 
    : token_duration_(7200) {  // 默认 2 小时有效期
    secret_key_ = "TinyWebServerSecretKey2024!@#";
    redis_ready_ = false;
}

bool AuthManager::initRedis(const std::string& host,
                            int port,
                            const std::string& password,
                            int db,
                            std::string* err) {
    // 初始化 Redis Cluster 客户端连接。
    // 该方法只初始化一次，后续业务操作依赖 isRedisReady() 判断连接状态。
    if (redis_.connect(host, port, password, db, err)) {
        redis_ready_ = true;
        return true;
    }
    redis_ready_ = false;
    return false;
}

bool AuthManager::isRedisReady() const {
    // 确保 RedisCluster 对象存在且状态正常。
    return redis_ready_ && redis_.isConnected();
}

bool AuthManager::ensureRedis(std::string* err) const {
    // 业务层调用前必须检查 Redis 是否已经初始化。
    if (!isRedisReady()) {
        if (err) {
            *err = "redis cluster not initialized";
        }
        return false;
    }
    return true;
}

// 生成指定长度的随机字符串，用作 token 的原始内容
std::string AuthManager::generateRandomString(size_t length) {
    static const char charset[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

// Base64 编码：将输入字节流分组为 3 字节并映射为 4 个 Base64 字符
std::string AuthManager::base64Encode(const std::string& input) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    const unsigned char* bytes_to_encode = reinterpret_cast<const unsigned char*>(input.c_str());
    size_t in_len = input.size();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i <4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

// Base64 解码：将 Base64 字符转换回原始字节
std::string AuthManager::base64Decode(const std::string& input) {
    int in_len = input.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (input[in_] != '=') && (isalnum(input[in_]) || (input[in_] == '+') || (input[in_] == '/'))) {
        char_array_4[i++] = input[in_]; in_++;
        if (i ==4) {
            for (i = 0; i <4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j <4; j++)
            char_array_4[j] = 0;

        for (j = 0; j <4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

        for (j = 0; (j < i - 1); j++)
            ret += char_array_3[j];
    }

    return ret;
}

// HMAC-SHA256 计算，返回原始二进制摘要字符串
std::string AuthManager::hmacSha256(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;

    HMAC(EVP_sha256(),
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest, &digest_len);

    return std::string(reinterpret_cast<char*>(digest), digest_len);
}

// 生成一个新的登录 token 并写入 Redis。
// token 内容包含 userId、username、过期时间戳，Redis TTL 负责自动过期。
// 同时将 username 写入 active_users Sorted Set，score 为过期时间。
std::string AuthManager::generateToken(int userId, const std::string& username) {
    std::string token = generateRandomString(32);           // 32 字符随机 token
    if (username.empty()) {
        return "";
    }

    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return "";
    }

    const auto now = std::chrono::system_clock::now();
    const auto expire_time = now + token_duration_;
    const long long expire_at = std::chrono::duration_cast<std::chrono::seconds>(expire_time.time_since_epoch()).count();
    const std::string value = std::to_string(userId) + "|" + username + "|" + std::to_string(expire_at);

    if (!redis_.setex(tokenKey(token), static_cast<int>(token_duration_.count()), value, &err)) {
        LOG_ERROR("Redis set token failed: %s", err.c_str());
        return "";
    }

    LOG_INFO("Generated token for user: %s (id: %d)", username.c_str(), userId);
    return token;
}

// 校验 token 是否存在且未过期，如果成功则写入 outInfo
// 验证 token 是否仍然有效。
// 读取 Redis token 值，解析 userId/username/expireAt，并判断是否过期。
// token 过期后会立即被删除。
bool AuthManager::validateToken(const std::string& token, UserInfo& outInfo) {
    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return false;
    }

    std::string value;
    bool found = false;
    if (!redis_.get(tokenKey(token), &value, &found, &err) || !found) {
        return false;
    }

    int userId = 0;
    std::string username;
    long long expire_at = 0;
    if (!splitTokenValue(value, &userId, &username, &expire_at)) {
        // 如果 token 格式异常，则删除无效值，避免后续重复失败。
        redis_.del(tokenKey(token), &err);
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const long long now_ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (expire_at <= now_ts) {
        redis_.del(tokenKey(token), &err);
        LOG_INFO("Token expired: %s", token.c_str());
        return false;
    }

    outInfo.id = userId;
    outInfo.username = username;
    outInfo.expire_time = std::chrono::system_clock::time_point(std::chrono::seconds(expire_at));
    return true;
}

// 判断用户名是否在已知用户列表中
bool AuthManager::hasKnownUsername(const std::string& username) {
    if (username.empty()) return false;

    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return false;
    }

    bool exists = false;
    if (!redis_.exists(userPwdKey(username), &exists, &err)) {
        return false;
    }
    return exists;
}

// 注册用户：如果用户名或密码为空则失败。
bool AuthManager::registerUser(const std::string& username, const std::string& password, std::string& err) {
    if (username.empty() || password.empty()) {
        err = "username and password are required";
        return false;
    }

    if (!ensureRedis(&err)) {
        return false;
    }

    bool exists = false;
    if (!redis_.exists(userPwdKey(username), &exists, &err)) {
        return false;
    }
    if (exists) {
        err = "Username already exists";
        return false;
    }

    if (!redis_.set(userPwdKey(username), password, &err)) {
        return false;
    }

    return true;
}

// 登录用户：如果用户名不存在则自动创建，支持兼容空密码旧记录。
// 当前实现使用 Redis Cluster 作为存储后端。
bool AuthManager::loginUser(const std::string& username, const std::string& password, std::string& err) {
    if (username.empty() || password.empty()) {
        err = "Username and password are required";
        return false;
    }

    if (!ensureRedis(&err)) {
        return false;
    }

    std::string stored;
    bool found = false;
    if (!redis_.get(userPwdKey(username), &stored, &found, &err)) {
        return false;
    }

    if (!found) {
        err = "User not registered, please register first";
        return false;
    }

    if (!stored.empty() && stored != password) {
        err = "Invalid password";
        return false;
    }

    if (stored.empty()) {
        if (!redis_.set(userPwdKey(username), password, &err)) {
            return false;
        }
    }

    return true;
}

// 发送好友请求：
// 1. 确认发送方和接收方都存在。
// 2. 确认两人尚未是好友关系。
// 3. 将请求写入 target 用户的 friend:requests 集合。
bool AuthManager::sendFriendRequest(const std::string& fromUser, const std::string& toUser, std::string& err) {
    if (fromUser.empty() || toUser.empty()) {
        err = "invalid username";
        return false;
    }
    if (fromUser == toUser) {
        err = "cannot add yourself";
        return false;
    }

    if (!ensureRedis(&err)) {
        return false;
    }

    bool fromExists = false;
    bool toExists = false;
    if (!redis_.exists(userPwdKey(fromUser), &fromExists, &err)) {
        return false;
    }
    if (!redis_.exists(userPwdKey(toUser), &toExists, &err)) {
        return false;
    }
    if (!fromExists || !toExists) {
        err = "user not found";
        return false;
    }

    bool alreadyFriends = false;
    if (!redis_.sismember(friendRelKey(fromUser), toUser, &alreadyFriends, &err)) {
        return false;
    }
    if (alreadyFriends) {
        err = "already friends";
        return false;
    }

    if (!redis_.sadd(friendReqKey(toUser), fromUser, &err)) {
        return false;
    }
    return true;
}

// 查询用户的所有待处理好友请求
std::vector<std::string> AuthManager::listPendingRequests(const std::string& toUser) {
    std::vector<std::string> pending;

    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return pending;
    }

    redis_.smembers(friendReqKey(toUser), &pending, &err);
    return pending;
}

// 处理好友请求，accept 为 true 表示同意，否则拒绝
bool AuthManager::verifyFriendRequest(const std::string& toUser, const std::string& fromUser, bool accept, std::string& err) {
    if (toUser.empty() || fromUser.empty()) {
        err = "invalid username";
        return false;
    }

    if (!ensureRedis(&err)) {
        return false;
    }

    bool requested = false;
    if (!redis_.sismember(friendReqKey(toUser), fromUser, &requested, &err)) {
        return false;
    }
    if (!requested) {
        err = "request not found";
        return false;
    }

    if (!redis_.srem(friendReqKey(toUser), fromUser, &err)) {
        return false;
    }

    if (accept) {
        if (!redis_.sadd(friendRelKey(toUser), fromUser, &err)) {
            return false;
        }
        if (!redis_.sadd(friendRelKey(fromUser), toUser, &err)) {
            return false;
        }
    }

    return true;
}

// 获取用户名的好友列表
std::vector<std::string> AuthManager::listFriends(const std::string& username) {
    std::vector<std::string> friends;

    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return friends;
    }

    redis_.smembers(friendRelKey(username), &friends, &err);
    return friends;
}

// 检查两个用户是否互为好友
bool AuthManager::areFriends(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;

    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return false;
    }

    bool ok = false;
    if (!redis_.sismember(friendRelKey(a), b, &ok, &err)) {
        return false;
    }
    return ok;
}

// 删除指定 token
void AuthManager::removeToken(const std::string& token) {
    std::string err;
    if (!ensureRedis(&err)) {
        LOG_ERROR("Redis not ready: %s", err.c_str());
        return;
    }

    redis_.del(tokenKey(token), &err);
    LOG_INFO("Removed token: %s", token.c_str());
}

// ============================================================
// MySQL ↔ Redis 双写同步方法
// ============================================================

bool AuthManager::syncUserPassword(const std::string& username, const std::string& password, std::string& err) {
    if (username.empty() || password.empty()) {
        err = "username and password are required";
        return false;
    }
    if (!ensureRedis(&err)) return false;
    return redis_.set(userPwdKey(username), password, &err);
}

bool AuthManager::syncFriendRelation(const std::string& userA, const std::string& userB, std::string& err) {
    if (userA.empty() || userB.empty()) {
        err = "invalid username";
        return false;
    }
    if (!ensureRedis(&err)) return false;

    if (!redis_.sadd(friendRelKey(userA), userB, &err)) return false;
    if (!redis_.sadd(friendRelKey(userB), userA, &err)) return false;
    return true;
}

bool AuthManager::syncFriendRequest(const std::string& from, const std::string& to, std::string& err) {
    if (from.empty() || to.empty()) {
        err = "invalid username";
        return false;
    }
    if (!ensureRedis(&err)) return false;
    return redis_.sadd(friendReqKey(to), from, &err);
}

void AuthManager::removeFriendRequest(const std::string& toUser, const std::string& fromUser) {
    std::string err;
    if (!ensureRedis(&err)) return;
    redis_.srem(friendReqKey(toUser), fromUser, &err);
}
