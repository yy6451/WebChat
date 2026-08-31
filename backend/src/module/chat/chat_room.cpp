/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/module/chat/chat_room.cpp
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
#include "chat_room.h"
#include "online_user_manager.h"
#include "core/connection/websocket_connection.h"
#include "utils/log/log.h"
#include <algorithm>
#include <sstream>
#include <ctime>
#include <cstring>

ChatRoom* ChatRoom::instance() {
    static ChatRoom room;
    return &room;
}

void ChatRoom::Join(WebSocketConnection* conn) {
    if (!conn) return;
    std::string username = conn->getUsername();
    if (username.empty()) {
        username = "user_" + std::to_string(conn->fd());
    }

    // 先在锁内添加连接
    mutex_.lock();
    connections_.insert(conn);
    user_index_[username] = conn;
    LOG_INFO("ChatRoom: connection %d joined, total=%zu", conn->fd(), connections_.size());
    mutex_.unlock();

    // 标记全集群在线状态
    OnlineUserManager::instance()->onUserOnline(username);

    
    // 发送用户加入通知给所有人
    uint64_t seq = OnlineUserManager::instance()->nextSeq(kDefaultRoomId);
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"system\","
        << "\"from\":\"system\","
        << "\"to\":\"" << kDefaultRoomId << "\","
        << "\"content\":\"" << username << " joined\","
        << "\"timestamp\":" << static_cast<long long>(time(nullptr)) << ","
        << "\"seq\":" << seq
        << "}";
    std::string joinMsg = oss.str();
    Broadcast(joinMsg, nullptr);
    
    // 广播在线用户列表给所有人
    BroadcastOnlineUsers();
}

void ChatRoom::Leave(WebSocketConnection* conn) {
    if (!conn) return;
    std::string username = conn->getUsername();
    if (username.empty()) {
        username = "user_" + std::to_string(conn->fd());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connections_.find(conn);
        if (it != connections_.end()) {
            connections_.erase(it);
            user_index_.erase(username);
            LOG_INFO("ChatRoom: connection %d left, total=%zu", conn->fd(), connections_.size());
        }
    }

    // 标记全集群离线
    OnlineUserManager::instance()->onUserOffline(username);

    // 发送用户离开通知
    uint64_t seq = OnlineUserManager::instance()->nextSeq(kDefaultRoomId);
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"system\","
        << "\"from\":\"system\","
        << "\"to\":\"" << kDefaultRoomId << "\","
        << "\"content\":\"" << username << " left\","
        << "\"timestamp\":" << static_cast<long long>(time(nullptr)) << ","
        << "\"seq\":" << seq
        << "}";
    std::string leaveMsg = oss.str();
    
    // 广播离开通知和更新在线用户列表
    Broadcast(leaveMsg, nullptr);
    BroadcastOnlineUsers();
}

void ChatRoom::Broadcast(const std::string& message, WebSocketConnection* exclude) {
    // 1. 在锁内拷贝当前连接列表（避免遍历期间被修改）
    std::vector<WebSocketConnection*> conns;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conns.reserve(connections_.size());
        for (auto conn : connections_) {
            conns.push_back(conn);
        }
    }

    // 2. 遍历拷贝的列表发送消息
    for (auto conn : conns) {
        if (conn == exclude) continue; // 跳过排除的连接

        // 发送消息（sendData 内部会处理写缓冲区，可能触发 EPOLLOUT）
        conn->sendData(message.c_str(), message.size(), WebSocketOpCode::TEXT);

        // 注意：sendData 仅将数据加入写缓冲区并注册写事件，不保证立即发送成功。
        // 若连接已关闭，sendData 内部会返回 false，但此处我们不做额外处理，
        // 因为连接关闭时会自动调用 Leave() 从集合中移除，下次广播就不会再包含它。
        // 但为了防止极少数情况（如连接刚关闭但尚未调用 Leave），我们可以检查一下：
        // 如果发送失败（比如 socket 已不可写），可考虑将其移除。但为简化，暂时不处理，
        // 因为连接关闭后会通过 Close() 调用 Leave，保证一致性。
    }

    LOG_DEBUG("ChatRoom broadcasted message (%zu bytes) to %zu connections (excluded %d)",
              message.size(), conns.size(), exclude ? exclude->fd() : -1);
}

void ChatRoom::BroadcastOnlineUsers() {
    // 从 Redis 获取全集群在线用户（跨服务器聚合）
    std::vector<std::string> users = OnlineUserManager::instance()->allOnlineUsers();
    if (users.empty()) {
        // Redis 不可用时回退到本机连接列表
        {
            std::lock_guard<std::mutex> lock(mutex_);
            users.reserve(connections_.size());
            for (auto c : connections_) {
                std::string u = c->getUsername();
                if (u.empty()) u = "user_" + std::to_string(c->fd());
                users.push_back(u);
            }
        }
    }

    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"online_users\","
        << "\"users\":[";
    for (size_t i = 0; i < users.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << users[i] << "\"";
    }
    oss << "]}";

    const std::string msg = oss.str();
    Broadcast(msg, nullptr);
}
bool ChatRoom::deliverLocal(const std::string& targetUser, const std::string& message) {
    if (targetUser.empty()) return false;

    WebSocketConnection* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_index_.find(targetUser);
        if (it == user_index_.end()) return false;
        target = it->second;
    }
    if (target) {
        target->sendData(message.c_str(), message.size(), WebSocketOpCode::TEXT);
        return true;
    }
    return false;
}

void ChatRoom::broadcastLocal(const std::string& message, WebSocketConnection* exclude) {
    std::vector<WebSocketConnection*> conns;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conns.reserve(connections_.size());
        for (auto conn : connections_) conns.push_back(conn);
    }
    for (auto conn : conns) {
        if (conn == exclude) continue;
        conn->sendData(message.c_str(), message.size(), WebSocketOpCode::TEXT);
    }
}

void ChatRoom::pingAll() {
    std::vector<WebSocketConnection*> conns;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conns.reserve(connections_.size());
        for (auto conn : connections_) conns.push_back(conn);
    }
    // 发送空 payload 的 PING 帧，浏览器 WebSocket 协议栈自动回复 PONG
    for (auto conn : conns) {
        conn->sendData("", 0, WebSocketOpCode::PING);
    }
}
