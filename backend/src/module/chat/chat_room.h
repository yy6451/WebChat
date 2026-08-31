/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/module/chat/chat_room.h
 * 类型: Header
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
#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <string>

// 前向声明（避免循环包含）
class WebSocketConnection;

/**
 * @brief 聊天室单例类，管理所有活跃的 WebSocket 连接。
 * 
 * 提供线程安全的 Join、Leave、Broadcast 操作。
 * 采用 C++11 静态局部变量实现线程安全的单例。
 */
class ChatRoom {
public:
    static constexpr const char* kDefaultRoomId = "room_default";
    static constexpr size_t kDefaultRoomMaxUsers = 500;

    // 获取单例实例
    static ChatRoom* instance();

    // 禁止拷贝和赋值
    ChatRoom(const ChatRoom&) = delete;
    ChatRoom& operator=(const ChatRoom&) = delete;

    /**
     * @brief 将连接加入聊天室。
     * @param conn 要加入的 WebSocket 连接指针（不能为 nullptr）。
     */
    void Join(WebSocketConnection* conn);

    /**
     * @brief 将连接从聊天室移除。
     * @param conn 要移除的连接指针（若不在集合中，则无操作）。
     */
    void Leave(WebSocketConnection* conn);

    /**
     * @brief 向所有在线连接广播消息。
     * @param message 要发送的消息内容（字符串）。
     * @param exclude 可选的排除连接（例如不向发送者自己广播），默认为 nullptr。
     * 
     * 该方法先拷贝当前连接列表，再逐个发送，避免长时间持有锁。
     * 发送过程中若发现连接已关闭，会自动将其移除（但通常应由连接主动调用 Leave）。
     */
    void Broadcast(const std::string& message, WebSocketConnection* exclude = nullptr);

    /**
     * @brief 仅本地投递（被 MessageDispatcher 回调使用）。
     *        不查询跨服务器在线状态，仅查本机 user_index_。
     * @return 是否找到目标
     */
    bool deliverLocal(const std::string& targetUser, const std::string& message);

    /**
     * @brief 仅本地广播（被 MessageDispatcher 回调使用）。
     *        不发布到跨服务器总线。
     */
    void broadcastLocal(const std::string& message, WebSocketConnection* exclude = nullptr);

    // 向所有 WebSocket 连接发送协议层 PING（浏览器自动回复 PONG，不受标签页节流影响）
    void pingAll();

    /**
     * @brief 广播在线用户列表。
     */
    void BroadcastOnlineUsers();

private:
    // 私有构造函数（单例）
    ChatRoom() = default;
    ~ChatRoom() = default;

private:
    mutable std::mutex mutex_;                          // 保护连接集合的互斥锁
    std::unordered_set<WebSocketConnection*> connections_; // 活跃连接集合
    std::unordered_map<std::string, WebSocketConnection*> user_index_; // 用户名到连接映射
};

#endif // CHAT_ROOM_H