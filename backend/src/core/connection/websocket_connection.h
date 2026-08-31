/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/websocket_connection.h
 * 类型: Header
 * 作用: 连接层模块：封装连接生命周期与协议分发（HTTP/WebSocket）。
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
#ifndef WEBSOCKET_CONNECTION_H
#define WEBSOCKET_CONNECTION_H

#include "connection.h"
#include "../protocol/websocket/websocket_frame.h"
#include <string>
#include <vector>

class WebSocketConnection : public Connection {
public:
    /**
     * 构造 WebSocketConnection
     * - `handshakeDone` 表示连接是否已完成握手（从 Http 升级过来时可设为 true）
     * - `username`/`userId` 用于业务标识
     */
    WebSocketConnection(int sockfd, const sockaddr_in& addr, int trigmode, int close_log, bool handshakeDone = false, const std::string& username = "", int userId = 0);
    /** 析构：释放与连接相关的资源（无特殊操作） */
    virtual ~WebSocketConnection();

    // Connection 接口实现
    const char* Type() const override { return "WebSocketConnection"; }
    /**
     * 读取 socket 数据到 `readBuffer_`。
     * - 如果尚未握手则用于接收 HTTP 握手请求，否则用于接收 WebSocket 帧数据。
     */
    virtual bool Read() override;
    /**
     * 将 `writeBuffer_` 中的数据写回 socket。
     */
    virtual bool Write() override;
    /**
     * 处理逻辑：未握手时尝试握手；握手后解析并处理 WebSocket 帧。
     */
    virtual void Process() override;
    /**
     * 关闭连接并通知业务层（聊天室）进行清理
     */
    virtual void Close() override;

    /**
     * 业务层调用：发送原始数据（会封装为 WebSocket 帧）
     */
    void sendData(const char* data, size_t len, WebSocketOpCode opcode = WebSocketOpCode::TEXT);

    // 访问器
    const std::string& getUsername() const { return username_; }
    int getUserId() const { return userId_; }

private:
    /**
     * 处理 WebSocket 握手（仅在未完成握手时调用）
     * - 使用 `HttpRequest` 解析缓冲区中的握手请求，并发送握手响应
     */
    bool handleHandshake();

    /**
     * 处理已解析出的 WebSocket 帧（路由到业务处理或内置响应）
     */
    void handleFrame(const WebSocketFrame& frame);

    /**
     * 编码并把帧放入 `writeBuffer_`（内部使用）
     */
    bool sendFrame(const char* data, size_t len, WebSocketOpCode opcode);

    /**
     * 发送关闭帧并可附带关闭原因
     */
    void sendClose(uint16_t code = 1000, const std::string& reason = "");

    /**
     * 计算 WebSocket 握手响应中 `Sec-WebSocket-Accept` 的值
     */
    std::string computeAccept(const std::string& key);

private:
    bool handshakeDone_;      // 是否已完成握手
    int trigmode_;            // 触发模式（ET/LT）
    int close_log_;           // 日志开关
    std::string username_;    // 业务层用户名
    int userId_;              // 用户 ID
    std::string sendBuffer_;  // 备用发送缓冲（当前未使用）
};

#endif // WEBSOCKET_CONNECTION_H