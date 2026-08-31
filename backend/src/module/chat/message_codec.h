#ifndef MODULE_CHAT_MESSAGE_CODEC_H
#define MODULE_CHAT_MESSAGE_CODEC_H

#include <string>
#include <cstdint>
#include <sstream>
#include <ctime>

// 消息编解码器：
// 1) 定义统一的 ChatMessage 结构体
// 2) JSON ↔ ChatMessage 双向转换
// 3) 生成全局唯一消息 ID
//
// 设计原则：协议层独立于业务层，后续切换到 Protobuf 等只需改此类。

class MessageCodec {
public:
    struct ChatMessage {
        std::string msg_id;          // 全局唯一消息 ID
        std::string server_id;       // 发送服务器 ID，用于去重
        std::string type;            // "chat" / "private" / "system"
        std::string from;            // 发送者用户名
        std::string to;              // 接收者（用户名或 "room_default"）
        std::string content;         // 消息内容
        int64_t timestamp;           // Unix 时间戳（秒）
        uint64_t seq;                // 消息序列号
        std::string target_server;   // 目标服务器 ID（私聊精准路由用，空表示广播）
        std::string target_role;     // "broadcast" / "unicast"

        ChatMessage()
            : timestamp(0), seq(0) {}
    };

    // JSON 字符串 → ChatMessage
    static ChatMessage decode(const std::string& json);

    // ChatMessage → JSON 字符串
    static std::string encode(const ChatMessage& msg);

    // 从已有的 WebSocket JSON（不含 __internal__ 字段）升级为 ChatMessage
    static ChatMessage fromLegacyJson(const std::string& json,
                                       const std::string& serverId,
                                       uint64_t seq);

    // 生成全局唯一消息 ID：timestamp-server_hash-increment
    static std::string generateMsgId(const std::string& serverId);

private:
    static uint64_t nextCounter();
};

#endif
