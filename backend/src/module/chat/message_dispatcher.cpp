#include "message_dispatcher.h"
#include "online_user_manager.h"
#include "chat_message_bus.h"
#include "../../utils/log/log.h"

MessageDispatcher* MessageDispatcher::instance() {
    static MessageDispatcher dispatcher;
    return &dispatcher;
}

void MessageDispatcher::dispatch(MessageCodec::ChatMessage& msg) {
    msg.server_id = server_id_;
    if (msg.msg_id.empty())
        msg.msg_id = MessageCodec::generateMsgId(server_id_);

    std::string encoded = MessageCodec::encode(msg);

    if (msg.to == "room_default" || msg.target_role == "broadcast") {
        // === 群聊: 本地广播 + 发布到总线 ===
        msg.target_role = "broadcast";
        msg.target_server.clear(); // 所有服务器都要收到
        encoded = MessageCodec::encode(msg);

        if (broadcast_cb_) broadcast_cb_(encoded);

        // 发布到跨服务器总线
        if (ChatMessageBus::instance()->isRunning())
            ChatMessageBus::instance()->publish(encoded);
    } else {
        // === 私聊: 查在线状态, 决定本地 or 远程 ===
        std::string targetServer = OnlineUserManager::instance()->whereIs(msg.to);

        if (targetServer.empty()) {
            // 用户不在线 → 标记离线消息（后续阶段实现）
            LOG_INFO("MessageDispatcher: user %s is offline", msg.to.c_str());
            // 仍然尝试本地投递（万一在本机）
            if (deliver_cb_) deliver_cb_(msg.to, encoded);
            return;
        }

        if (targetServer == server_id_) {
            // 目标在本机 → 本地投递（回显已在 ChatService 中通过 conn->sendData() 完成）
            if (deliver_cb_) deliver_cb_(msg.to, encoded);
        } else {
            // 目标在其他服务器 → 走总线
            msg.target_role = "unicast";
            msg.target_server = targetServer;
            encoded = MessageCodec::encode(msg);

            if (ChatMessageBus::instance()->isRunning())
                ChatMessageBus::instance()->publish(encoded);
        }
    }
}

void MessageDispatcher::onBusMessage(const std::string& msgId, const std::string& data) {
    (void)msgId; // XACK 已在 ChatMessageBus 中处理

    MessageCodec::ChatMessage msg = MessageCodec::decode(data);

    // 去重：自己发的消息跳过（已在 dispatch 中本地投递）
    if (msg.server_id == server_id_) return;

    if (msg.target_role == "broadcast" || msg.to == "room_default") {
        // 群聊 → 本地广播
        if (broadcast_cb_)
            broadcast_cb_(data);
    } else if (msg.target_role == "unicast") {
        // 私聊 → 检查目标是否在本机
        if (!msg.target_server.empty() && msg.target_server != server_id_)
            return; // 不是给我的
        if (deliver_cb_)
            deliver_cb_(msg.to, data);
    }
}
