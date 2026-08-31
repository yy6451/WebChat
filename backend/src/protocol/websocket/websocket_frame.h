/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/websocket/websocket_frame.h
 * 类型: Header
 * 作用: WebSocket 协议模块：负责帧编解码、掩码处理与消息边界管理。
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
#ifndef WEBSOCKET_FRAME_H
#define WEBSOCKET_FRAME_H

#include <cstdint>
#include <vector>

// WebSocket 操作码定义
enum class WebSocketOpCode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket 帧结构（仅用于解析时临时存储，实际使用时通常直接解析为数据）
struct WebSocketFrame {
    bool fin;                   // 是否为最后一帧
    bool rsv1, rsv2, rsv3;      // 保留位，通常为 0
    WebSocketOpCode opcode;      // 操作码
    bool masked;                 // 是否有掩码
    uint64_t payloadLength;      // 负载长度（7/7+16/7+64）
    uint32_t maskingKey;         // 掩码键（如果有）
    std::vector<char> payload;   // 负载数据（已去掩码）

    WebSocketFrame()
        : fin(false), rsv1(false), rsv2(false), rsv3(false),
          opcode(WebSocketOpCode::TEXT), masked(false),
          payloadLength(0), maskingKey(0) {}
};

#endif // WEBSOCKET_FRAME_H