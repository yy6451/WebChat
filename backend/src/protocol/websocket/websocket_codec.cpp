/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/websocket/websocket_codec.cpp
 * 类型: Source
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
#include "websocket_codec.h"
#include "../../buffer/buffer.h"
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <random>

static uint32_t GenerateMaskingKey() {
    static thread_local std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<uint32_t> dist;
    return dist(rng);
}

// 编码 WebSocket 帧
std::string EncodeWebSocketFrame(const char* data, size_t len, WebSocketOpCode opcode, bool isClient) {
    std::string frame;
    uint8_t header = 0x80; // FIN = 1, RSV = 0
    header |= static_cast<uint8_t>(opcode);
    frame.push_back(static_cast<char>(header));

    // 负载长度处理
    size_t payloadLen = len;
    if (payloadLen < 126) {
        uint8_t lenByte = static_cast<uint8_t>(payloadLen);
        if (isClient) {
            lenByte |= 0x80; // 客户端必须设置掩码位
        }
        frame.push_back(static_cast<char>(lenByte));
    } else if (payloadLen <= 0xFFFF) {
        uint8_t lenByte = 126;
        if (isClient) {
            lenByte |= 0x80;
        }
        frame.push_back(static_cast<char>(lenByte));
        uint16_t netLen = htons(static_cast<uint16_t>(payloadLen));
        frame.append(reinterpret_cast<char*>(&netLen), 2);
    } else {
        uint8_t lenByte = 127;
        if (isClient) {
            lenByte |= 0x80;
        }
        frame.push_back(static_cast<char>(lenByte));
        uint64_t netLen = htobe64(payloadLen); // 网络字节序（大端）
        frame.append(reinterpret_cast<char*>(&netLen), 8);
    }

    // 如果是客户端发送，需要添加掩码键
    uint32_t maskingKey = 0;
    if (isClient) {
        // 客户端掩码键应为随机值
        maskingKey = GenerateMaskingKey();
        frame.append(reinterpret_cast<char*>(&maskingKey), 4);
    }

    // 添加负载数据（如果需要掩码，则对数据进行掩码处理）
    if (isClient) {
        // 对数据逐字节掩码
        char* keyBytes = reinterpret_cast<char*>(&maskingKey);
        for (size_t i = 0; i < len; ++i) {
            frame.push_back(data[i] ^ keyBytes[i % 4]);
        }
    } else {
        // 服务器发送不需掩码，直接追加
        frame.append(data, len);
    }

    return frame;
}

// 解码 WebSocket 帧
bool DecodeWebSocketFrame(Buffer* input, WebSocketFrame& frame) {
    if (input->ReadableBytes() < 2) {
        return false; // 至少需要2字节头部
    }

    const char* data = input->Peek();
    uint8_t header1 = static_cast<uint8_t>(data[0]);
    uint8_t header2 = static_cast<uint8_t>(data[1]);

    frame.fin = (header1 & 0x80) != 0;
    frame.opcode = static_cast<WebSocketOpCode>(header1 & 0x0F);
    frame.masked = (header2 & 0x80) != 0;

    size_t payloadLen = header2 & 0x7F;
    size_t headerLen = 2;

    if (payloadLen == 126) {
        if (input->ReadableBytes() < 4) return false;
        frame.payloadLength = ntohs(*reinterpret_cast<const uint16_t*>(data + 2));
        headerLen = 4;
    } else if (payloadLen == 127) {
        if (input->ReadableBytes() < 10) return false;
        frame.payloadLength = be64toh(*reinterpret_cast<const uint64_t*>(data + 2));
        headerLen = 10;
    } else {
        frame.payloadLength = payloadLen;
    }

    if (frame.masked) {
        if (input->ReadableBytes() < headerLen + 4) return false;
        frame.maskingKey = *reinterpret_cast<const uint32_t*>(data + headerLen);
        headerLen += 4;
    }

    if (input->ReadableBytes() < headerLen + frame.payloadLength) return false;

    // 提取负载数据
    frame.payload.assign(data + headerLen, data + headerLen + frame.payloadLength);

    // 如果有掩码，解码
    if (frame.masked) {
        for (size_t i = 0; i < frame.payloadLength; ++i) {
            frame.payload[i] ^= reinterpret_cast<const char*>(&frame.maskingKey)[i % 4];
        }
    }

    // 从缓冲区中移除已处理的帧
    input->Retrieve(headerLen + frame.payloadLength);

    return true;
}

// 解码 WebSocket 帧（从 Buffer 中读取一个完整帧，返回负载数据）
// 成功返回 true，并将负载存入 output；失败返回 false
bool DecodeWebSocketFrame(Buffer* input, std::vector<char>& output) {
    // 至少需要 2 字节才能解析基本头
    if (input->ReadableBytes() < 2) {
        return false;
    }

    const char* data = input->Peek();

    uint8_t firstByte = static_cast<uint8_t>(data[0]);
    uint8_t secondByte = static_cast<uint8_t>(data[1]);

    bool fin = (firstByte & 0x80) != 0;
    uint8_t opcode = firstByte & 0x0F;
    bool masked = (secondByte & 0x80) != 0;
    uint64_t payloadLen = secondByte & 0x7F;

    size_t headerLen = 2; // 基本头长度

    // 解析扩展负载长度
    if (payloadLen == 126) {
        if (input->ReadableBytes() < 4) { // 2 + 2
            return false;
        }
        uint16_t netLen;
        memcpy(&netLen, data + 2, 2);
        payloadLen = ntohs(netLen);
        headerLen += 2;
    } else if (payloadLen == 127) {
        if (input->ReadableBytes() < 10) { // 2 + 8
            return false;
        }
        uint64_t netLen;
        memcpy(&netLen, data + 2, 8);
        payloadLen = be64toh(netLen);
        headerLen += 8;
    }

    // 如果有掩码，读取掩码键
    uint32_t maskingKey = 0;
    if (masked) {
        if (input->ReadableBytes() < headerLen + 4) {
            return false;
        }
        memcpy(&maskingKey, data + headerLen, 4);
        headerLen += 4;
    }

    // 检查是否有完整的负载数据
    if (input->ReadableBytes() < headerLen + payloadLen) {
        return false;
    }

    // 提取负载数据
    const char* payloadData = data + headerLen;
    output.resize(payloadLen);
    if (masked) {
        // 需要去掩码
        const char* key = reinterpret_cast<const char*>(&maskingKey);
        for (uint64_t i = 0; i < payloadLen; ++i) {
            output[i] = payloadData[i] ^ key[i % 4];
        }
    } else {
        memcpy(output.data(), payloadData, payloadLen);
    }

    // 从缓冲区中移除已处理的帧
    input->Retrieve(headerLen + payloadLen);

    return true;
}