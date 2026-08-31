/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/websocket/websocket_codec.h
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
#ifndef WEBSOCKET_CODEC_H
#define WEBSOCKET_CODEC_H

#include "websocket_frame.h"
#include <string>
#include <vector>

class Buffer;

std::string EncodeWebSocketFrame(const char* data, size_t len, WebSocketOpCode opcode, bool isClient = false);
bool DecodeWebSocketFrame(Buffer* input, WebSocketFrame& frame);

#endif