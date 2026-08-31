/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/module/chat/chat_service.h
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
#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include <string>
#include <functional>
#include <memory>

class connection_pool;

// 前向声明
class WebSocketConnection;

/**
 * @brief 聊天业务处理类（单例）
 * 
 * 负责解析客户端消息，执行相应的业务逻辑（如广播、私聊、存储消息等）。
 * 通过依赖注入方式持有数据库连接池指针，以便需要时操作数据库。
 */
class ChatService {
public:
    // 获取单例实例（传入数据库连接池指针，只需在初始化时调用一次）
    static ChatService* instance(connection_pool* connPool = nullptr);

    // 禁止拷贝和赋值
    ChatService(const ChatService&) = delete;
    ChatService& operator=(const ChatService&) = delete;

    /**
     * @brief 处理从 WebSocket 连接收到的消息
     * @param conn 发送消息的连接
     * @param data 消息数据（UTF-8 文本或二进制）
     * @param len 数据长度
     * @param isText 是否为文本消息（true 表示文本，false 表示二进制）
     */
    void handleMessage(WebSocketConnection* conn, const char* data, size_t len, bool isText);

private:
    // 私有构造函数
    explicit ChatService(connection_pool* connPool);
    ~ChatService() = default;

    // 解析 JSON 消息并执行相应操作
    void processJsonMessage(WebSocketConnection* conn, const std::string& jsonStr);

    // 可选的数据库操作示例
    void saveMessageToDB(int fromUserId, int toUserId, const std::string& msg);
    int queryUserIdByUsername(const std::string& username);

private:
    connection_pool* connPool_;   // 数据库连接池（可选）
};

#endif // CHAT_SERVICE_H