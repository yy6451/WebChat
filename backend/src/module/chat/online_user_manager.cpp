#include "online_user_manager.h"
#include "../../utils/log/log.h"

OnlineUserManager* OnlineUserManager::instance() {
    static OnlineUserManager mgr;
    return &mgr;
}

OnlineUserManager::OnlineUserManager() : redis_ready_(false) {}

bool OnlineUserManager::initRedis(const std::string& host, int port,
                                  const std::string& password, std::string* err) {
    if (redis_.connect(host, port, password, 0, err)) {
        redis_ready_ = true;
        return true;
    }
    redis_ready_ = false;
    return false;
}

void OnlineUserManager::onUserOnline(const std::string& username) {
    if (username.empty()) return;
    std::string err;
    redis_.setex(onlineKey(username), kOnlineTTL, server_id_, &err);
    redis_.sadd(allOnlineKey(), username, &err);
    LOG_INFO("OnlineUser: %s joined at %s", username.c_str(), server_id_.c_str());
}

void OnlineUserManager::onUserOffline(const std::string& username) {
    if (username.empty()) return;
    std::string err;
    redis_.del(onlineKey(username), &err);
    redis_.srem(allOnlineKey(), username, &err);
    LOG_INFO("OnlineUser: %s left", username.c_str());
}

std::string OnlineUserManager::whereIs(const std::string& username) {
    if (username.empty()) return "";
    std::string server, err;
    bool found = false;
    if (redis_.get(onlineKey(username), &server, &found, &err) && found) {
        return server;
    }
    return "";
}

void OnlineUserManager::refreshHeartbeat(const std::string& username) {
    if (username.empty()) return;
    std::string err;
    // SETEX 不带 value? No — Redis SETEX 必须带 value.
    // 这里直接重新 SETEX，value 保持 server_id_
    redis_.setex(onlineKey(username), kOnlineTTL, server_id_, &err);
}

uint64_t OnlineUserManager::nextSeq(const std::string& roomId) {
    if (roomId.empty() || !redis_ready_) return 0;
    std::string err;
    long long seq = 0;
    if (!redis_.incr(seqKey(roomId), &seq, &err)) {
        LOG_ERROR("Redis incr seq failed: %s", err.c_str());
        return 0;
    }
    return static_cast<uint64_t>(seq);
}

std::vector<std::string> OnlineUserManager::allOnlineUsers() {
    std::vector<std::string> users;
    if (!redis_ready_) return users;
    std::string err;
    redis_.smembers(allOnlineKey(), &users, &err);
    return users;
}
