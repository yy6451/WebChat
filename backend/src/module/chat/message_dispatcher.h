#ifndef MODULE_CHAT_MESSAGE_DISPATCHER_H
#define MODULE_CHAT_MESSAGE_DISPATCHER_H

#include <string>
#include <functional>
#include "message_codec.h"

// 消息路由决策器。
//
// 职责: 决定消息走本地投递还是走跨服务器总线。
//
// 设计意图: 把「消息去哪」的决策逻辑从 ChatRoom 中抽离，
// ChatRoom 只负责本机的 WebSocket 连接集合。

// 本地投递回调: 目标用户名 → 消息 JSON → 是否成功
using DeliverCallback = std::function<bool(const std::string& targetUser, const std::string& message)>;
// 广播回调: 消息 JSON → void
using BroadcastCallback = std::function<void(const std::string& message)>;

class MessageDispatcher {
public:
    static MessageDispatcher* instance();

    MessageDispatcher(const MessageDispatcher&) = delete;
    MessageDispatcher& operator=(const MessageDispatcher&) = delete;

    void setServerId(const std::string& id) { server_id_ = id; }
    const std::string& serverId() const { return server_id_; }

    // 注册本地投递回调（由 ChatRoom 提供）
    void setDeliverCallback(DeliverCallback cb) { deliver_cb_ = std::move(cb); }
    void setBroadcastCallback(BroadcastCallback cb) { broadcast_cb_ = std::move(cb); }

    // 发送消息入口
    // 群聊: to == "room_default" → 本地广播 + 发布到总线
    // 私聊: to == username    → 本地投递 or 发布到总线
    void dispatch(MessageCodec::ChatMessage& msg);

    // 收到总线消息的回调（由 ChatMessageBus 消费线程调用）
    void onBusMessage(const std::string& msgId, const std::string& data);

private:
    MessageDispatcher() = default;

    std::string server_id_;
    DeliverCallback deliver_cb_;
    BroadcastCallback broadcast_cb_;
};

#endif
