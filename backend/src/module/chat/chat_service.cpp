/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/module/chat/chat_service.cpp
 * 类型: Source
 * 作用: 聊天业务模块：负责房间管理、好友关系约束、消息路由与状态维护。
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
#include "chat_service.h"
#include "chat_room.h"
#include "message_dispatcher.h"
#include "message_codec.h"
#include "online_user_manager.h"
#include "core/connection/websocket_connection.h"
#include "module/http/repository/user_repository.h"
#include "utils/log/log.h"
#include "utils/sql/sql_connection_pool.h"
#include "utils/auth/auth_manager.h"
#include <memory>
#include <cstring>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cstdlib>

namespace {

std::string normalizeUser(WebSocketConnection* conn) {
    if (!conn) return "";
    std::string user = conn->getUsername();
    if (user.empty()) {
        user = "user_" + std::to_string(conn->fd());
    }
    return user;
}

std::string extractJsonStringField(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";

    return json.substr(pos + 1, end - pos - 1);
}

bool looksLikeJson(const std::string& payload) {
    return !payload.empty() && payload.front() == '{' && payload.back() == '}';
}

std::string escapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool canPrivateChat(connection_pool* connPool, int fromId, int toId, const std::string& fromUser, const std::string& toUser) {
    if (fromUser.empty() || toUser.empty()) return false;

    // DB 模式：查 friend_relation 表确认好友关系
    if (connPool && fromId > 0 && toId > 0) {
        MYSQL* mysql = connPool->GetConnection();
        if (!mysql) return false;
        std::string err;
        UserRepository repo(mysql);
        bool ok = repo.AreFriends(fromId, toId, err);
        connPool->ReleaseConnection(mysql);
        return ok;
    }

    // 非 DB 模式下使用 Redis Cluster 中的好友关系。
    return AuthManager::instance()->areFriends(fromUser, toUser);
}

} // namespace

ChatService* ChatService::instance(connection_pool* connPool) {
    static ChatService service(connPool);
    return &service;
}

ChatService::ChatService(connection_pool* connPool)
    : connPool_(connPool) {
    LOG_INFO("ChatService initialized with connection pool: %p", connPool);
}

void ChatService::handleMessage(WebSocketConnection* conn, const char* data, size_t len, bool isText) {
    if (!conn || !data || len == 0) return;

    if (isText) {
        // 文本消息，直接广播
        std::string msg(data, len);
        processJsonMessage(conn, msg);
    } else {
        // 二进制消息：可扩展为其他业务（如图片、文件），此处简单广播二进制数据
        LOG_INFO("Received binary message from %d, size=%zu", conn->fd(), len);
        // 示例：广播二进制数据（需要定义二进制消息格式，此处仅作演示）
        // ChatRoom::instance()->BroadcastBinary(data, len, conn);
    }
}

void ChatService::processJsonMessage(WebSocketConnection* conn, const std::string& jsonStr) {
    if (!conn) return;

    const std::string user = normalizeUser(conn);
    const std::string roomId = ChatRoom::kDefaultRoomId;

    std::string type = "chat";
    std::string content = jsonStr;
    std::string to = roomId;

    if (looksLikeJson(jsonStr)) {
        const std::string t = extractJsonStringField(jsonStr, "type");
        const std::string c = extractJsonStringField(jsonStr, "content");
        const std::string target = extractJsonStringField(jsonStr, "to");
        if (!t.empty()) type = t;
        if (!c.empty()) content = c;
        if (!target.empty()) to = target;
    }

    const uint64_t seq = OnlineUserManager::instance()->nextSeq(roomId);

    std::string payloadType = "chat";
    if (type == "private") {
        payloadType = "private";
    }

    const std::string safeFrom = escapeJson(user);
    const std::string safeTo = escapeJson(to);
    const std::string safeContent = escapeJson(content);

    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"" << payloadType << "\","
        << "\"from\":\"" << safeFrom << "\","
        << "\"to\":\"" << safeTo << "\","
        << "\"content\":\"" << safeContent << "\","
        << "\"timestamp\":" << static_cast<long long>(time(nullptr)) << ","
        << "\"seq\":" << seq
        << "}";
    std::string message = oss.str();

    if (payloadType == "private" && !to.empty() && to != roomId) {
        int fromId = conn->getUserId();
        int toId = queryUserIdByUsername(to);
        if (!canPrivateChat(connPool_, fromId, toId, user, to)) {
            std::ostringstream err;
            err << "{"
                << "\"type\":\"system\"," 
                << "\"from\":\"system\"," 
                << "\"to\":\"" << safeFrom << "\"," 
                << "\"content\":\"friend verification required\"," 
                << "\"timestamp\":" << static_cast<long long>(time(nullptr))
                << "}";
            const std::string errMsg = err.str();
            conn->sendData(errMsg.c_str(), errMsg.size(), WebSocketOpCode::TEXT);
            return;
        }

        // 私聊：通过 MessageDispatcher 路由（自动判断本地/远程）
        MessageCodec::ChatMessage cmsg = MessageCodec::fromLegacyJson(message,
            MessageDispatcher::instance()->serverId(), seq);
        cmsg.type = "private";
        cmsg.from = user;
        cmsg.to = to;
        cmsg.content = content;

        // 先回显给发送者
        conn->sendData(message.c_str(), message.size(), WebSocketOpCode::TEXT);

        if (fromId > 0 && toId > 0) {
            saveMessageToDB(fromId, toId, content);
        }

        MessageDispatcher::instance()->dispatch(cmsg);
        return;
    }

    // 群聊广播 — 通过 MessageDispatcher 路由
    MessageCodec::ChatMessage cmsg = MessageCodec::fromLegacyJson(message,
        MessageDispatcher::instance()->serverId(), seq);
    cmsg.type = "chat";
    cmsg.from = user;
    cmsg.to = roomId;
    MessageDispatcher::instance()->dispatch(cmsg);
}

void ChatService::saveMessageToDB(int fromUserId, int toUserId, const std::string& msg) {
    if (!connPool_) return;

    MYSQL* mysql = connPool_->GetConnection();
    if (!mysql) {
        LOG_ERROR("Failed to get database connection");
        return;
    }

    std::string err;
    UserRepository repo(mysql);
    repo.SaveMessage(fromUserId, toUserId, msg, err);
    if (!err.empty()) {
        LOG_ERROR("SaveMessage failed: %s", err.c_str());
    }

    connPool_->ReleaseConnection(mysql);
}

int ChatService::queryUserIdByUsername(const std::string& username) {
    if (!connPool_ || username.empty()) return 0;

    MYSQL* mysql = connPool_->GetConnection();
    if (!mysql) return 0;

    std::string err;
    int uid = 0;
    UserRepository repo(mysql);
    repo.FindUserIdByName(username, uid, err);

    connPool_->ReleaseConnection(mysql);
    return uid;
}