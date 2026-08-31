#ifndef MODULE_CHAT_CHAT_MESSAGE_BUS_H
#define MODULE_CHAT_CHAT_MESSAGE_BUS_H

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include "../../utils/redis/redis_client.h"

// 跨服务器消息总线，封装 Redis Streams + Consumer Group。
//
// 职责:
//   1) 启动时创建 Consumer Group（幂等）
//   2) XADD 发布消息到 Stream
//   3) 独立线程 XREADGROUP 消费消息
//   4) 消费回调委托给 MessageDispatcher 处理
//
// 线程模型:
//   主线程(epoll)  → publish() → XADD
//   消费线程       → consumeLoop() → XREADGROUP → callback → XACK

class ChatMessageBus {
public:
    using MessageCallback = std::function<void(const std::string& msgId, const std::string& data)>;

    static ChatMessageBus* instance();

    ChatMessageBus(const ChatMessageBus&) = delete;
    ChatMessageBus& operator=(const ChatMessageBus&) = delete;

    // 初始化: 连接 Redis → 创建 Consumer Group → 启动消费线程
    // @param serverId: 本服务器标识, 用于 Consumer Group 命名
    bool init(const std::string& host, int port,
              const std::string& password, const std::string& serverId,
              std::string* err);

    // 发布消息到 Stream
    void publish(const std::string& data);

    // 设置收到消息的回调（由 MessageDispatcher 注册）
    void setMessageCallback(MessageCallback cb);

    // 优雅关闭
    void shutdown();

    bool isRunning() const { return running_.load(); }

private:
    ChatMessageBus();
    ~ChatMessageBus();

    void consumeLoop();
    void ackMessage(const std::string& msgId);

    static const char* kStreamKey;
    static const long long kMaxLen = 10000;

    std::string server_id_;
    std::string consumer_group_;
    std::string consumer_name_;
    RedisClient redis_;
    MessageCallback on_message_;
    std::thread consume_thread_;
    std::atomic<bool> running_;
};

#endif
