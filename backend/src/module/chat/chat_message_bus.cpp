#include "chat_message_bus.h"
#include "message_codec.h"
#include "../../utils/log/log.h"
#include <chrono>

const char* ChatMessageBus::kStreamKey = "chat:{messages}";

ChatMessageBus* ChatMessageBus::instance() {
    static ChatMessageBus bus;
    return &bus;
}

ChatMessageBus::ChatMessageBus() : running_(false) {}

ChatMessageBus::~ChatMessageBus() {
    shutdown();
}

bool ChatMessageBus::init(const std::string& host, int port,
                           const std::string& password, const std::string& serverId,
                           std::string* err) {
    server_id_ = serverId;
    consumer_group_ = "group:server-" + serverId;
    consumer_name_ = "consumer-1";

    if (!redis_.connect(host, port, password, 0, err)) {
        return false;
    }

    // 幂等创建 Consumer Group（已存在则返回成功）
    redis_.xgroup_create(kStreamKey, consumer_group_, "$", true, err);
    LOG_INFO("ChatMessageBus initialized: server=%s, group=%s",
             server_id_.c_str(), consumer_group_.c_str());

    running_ = true;
    consume_thread_ = std::thread(&ChatMessageBus::consumeLoop, this);
    return true;
}

void ChatMessageBus::publish(const std::string& data) {
    if (!running_) return;

    std::string msgId, err;
    std::vector<std::pair<std::string, std::string>> fields = {{"data", data}};

    if (!redis_.xadd(kStreamKey, "*", fields, &msgId, &err)) {
        LOG_ERROR("ChatMessageBus XADD failed: %s", err.c_str());
        return;
    }
    // 裁剪旧消息
    redis_.xtrim_maxlen(kStreamKey, kMaxLen, &err);
}

void ChatMessageBus::setMessageCallback(MessageCallback cb) {
    on_message_ = std::move(cb);
}

void ChatMessageBus::shutdown() {
    if (!running_) return;
    running_ = false;
    if (consume_thread_.joinable()) {
        consume_thread_.join();
    }
    LOG_INFO("ChatMessageBus shutdown complete");
}

void ChatMessageBus::consumeLoop() {
    LOG_INFO("ChatMessageBus consumeLoop started");
    while (running_) {
        std::vector<std::vector<std::pair<std::string, std::string>>> messages;
        std::string err;

        if (!redis_.xreadgroup(consumer_group_, consumer_name_,
                kStreamKey, ">", 100, 1000, &messages, &err)) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        for (auto& fields : messages) {
            std::string msgId;
            std::string data;
            for (auto& [k, v] : fields) {
                if (k == "__msg_id") msgId = v;
                else if (k == "data") data = v;
            }

            if (!data.empty() && on_message_) {
                on_message_(msgId, data);
            }
            ackMessage(msgId);
        }
    }
    LOG_INFO("ChatMessageBus consumeLoop exited");
}

void ChatMessageBus::ackMessage(const std::string& msgId) {
    std::string err;
    redis_.xack(kStreamKey, consumer_group_, msgId, &err);
}
