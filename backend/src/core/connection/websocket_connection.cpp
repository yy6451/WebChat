/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/websocket_connection.cpp
 * 类型: Source
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
#include "websocket_connection.h"
#include "../protocol/websocket/websocket_codec.h"
#include "../protocol/http/http_request.h"
#include "../module/chat/chat_service.h"
#include "../module/chat/chat_room.h"
#include "../module/chat/online_user_manager.h"
#include "../webserver.h"
#include "../utils/log/log.h"
#include "../utils/timer/lst_timer.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <cstring>
#include <errno.h>
#include <sys/socket.h>

// 统一使用 Utils 单例：`Utils::Instance().modfd(...)` / `removefd(...)` 等

// WebSocket 握手所需的魔术字符串
static constexpr const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WebSocketConnection::WebSocketConnection(int sockfd, const sockaddr_in& addr, int trigmode, int close_log, bool handshakeDone, const std::string& username, int userId)
    : Connection(sockfd, addr),
      handshakeDone_(handshakeDone),
      trigmode_(trigmode),
      close_log_(close_log),
    username_(username),
    userId_(userId)
{
    if (username_.empty()) {
        username_ = "user_" + std::to_string(sockfd_);
    }
    // 初始状态：等待握手请求
}

WebSocketConnection::~WebSocketConnection()
{
    // 无需额外清理
}

bool WebSocketConnection::Read()
{
    LOG_INFO("WebSocket Read() called for fd: %d, handshakeDone=%d", sockfd_, handshakeDone_);
    int saveErrno = 0;
    ssize_t n = readBuffer_.ReadFd(sockfd_, &saveErrno);
    if (n < 0) {
        LOG_ERROR("WebSocket read error: %d", saveErrno);
        return false;
    }
    LOG_INFO("WebSocket Read() got %d bytes", (int)n);
    return true;
}

bool WebSocketConnection::Write()
{
    LOG_INFO("WebSocket Write() called for fd: %d, readable bytes=%d", sockfd_, (int)writeBuffer_.ReadableBytes());
    // 将 writeBuffer_ 中的数据发送出去
    ssize_t n = writeBuffer_.WriteFd(sockfd_, nullptr);
    if (n < 0) {
        LOG_ERROR("WebSocket write error: %d", errno);
        return false;
    }
    LOG_INFO("WebSocket Write() sent %d bytes, remaining=%d", (int)n, (int)writeBuffer_.ReadableBytes());
    // 如果还有数据未发完，保持 EPOLLOUT 监听；否则转为 EPOLLIN
    if (writeBuffer_.ReadableBytes() == 0) {
        Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
    }
    return true;
}

void WebSocketConnection::Process()
{
    LOG_INFO("WebSocketConnection::Process() called for fd: %d, handshakeDone: %d", sockfd_, handshakeDone_);
    if (!handshakeDone_) {
        // 尚未完成握手，尝试握手
        if (!handleHandshake()) {
            // 握手失败，关闭连接
            Close();
        }
        return;
    }

    // 已握手，循环处理所有完整帧
    while (true) {
        WebSocketFrame frame;
        bool ret = DecodeWebSocketFrame(&readBuffer_, frame);
        if (!ret) {
            LOG_INFO("No complete WebSocket frame available yet for fd: %d", sockfd_);
            break; // 没有完整帧，等待更多数据
        }

        LOG_INFO("WebSocket frame decoded for fd: %d, opcode: %d, payload len: %zu", sockfd_, (int)frame.opcode, frame.payload.size());
        handleFrame(frame);
    }

    // 仅当没有待发送数据时重置为读事件，避免覆盖 sendFrame 设置的 EPOLLOUT。
    if (sockfd_ != -1 && writeBuffer_.ReadableBytes() == 0) {
        Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
    }
}

void WebSocketConnection::Close()
{
    if (sockfd_ != -1) {
        // 从 epoll 移除并关闭 socket
        Utils::Instance().removefd(Connection::m_epollfd, sockfd_);
        sockfd_ = -1;
        Connection::m_user_count--;
    }
    // 从聊天室移除此连接
    ChatRoom::instance()->Leave(this);
}

bool WebSocketConnection::handleHandshake()
{
    // 使用 HttpRequest 解析握手请求
    HttpRequest req;
    size_t len = readBuffer_.ReadableBytes();
    const char* data = readBuffer_.Peek();
    HttpRequest::ParseState state = req.parse(data, len);

    if (state == HttpRequest::kParseError) {
        LOG_ERROR("WebSocket handshake parse error");
        return false;
    }
    if (state != HttpRequest::kParseGotAll) {
        // 需要更多数据
        return false;
    }

    // 检查是否为 WebSocket 升级请求
    std::string upgrade = req.getHeader("upgrade");
    if (upgrade.empty() || strcasecmp(upgrade.c_str(), "websocket") != 0) {
        LOG_ERROR("Not a WebSocket upgrade request");
        return false;
    }

    std::string connection = req.getHeader("connection");
    if (connection.empty() || connection.find("Upgrade") == std::string::npos) {
        LOG_ERROR("Connection header missing Upgrade");
        return false;
    }

    std::string key = req.getHeader("sec-websocket-key");
    if (key.empty()) {
        LOG_ERROR("Missing Sec-WebSocket-Key");
        return false;
    }

    std::string version = req.getHeader("sec-websocket-version");
    if (version != "13") {
        LOG_ERROR("Unsupported WebSocket version: %s", version.c_str());
        return false;
    }

    // 计算 Sec-WebSocket-Accept
    std::string accept = computeAccept(key);

    // 构建响应
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    // 发送响应
    int n = send(sockfd_, response.c_str(), response.size(), 0);
    if (n <= 0) {
        LOG_ERROR("Failed to send handshake response");
        return false;
    }

    // 从读缓冲区中移除已处理的握手请求
    readBuffer_.Retrieve(req.parsedBytes());

    handshakeDone_ = true;
    Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
    LOG_INFO("WebSocket handshake successful for fd %d", sockfd_);
    return true;
}

std::string WebSocketConnection::computeAccept(const std::string& key)
{
    std::string combined = key + WS_MAGIC;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);

    // base64 编码
    char base64[64];
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(base64), hash, SHA_DIGEST_LENGTH);
    return std::string(base64, len);
}

void WebSocketConnection::handleFrame(const WebSocketFrame &frame)
{
    LOG_INFO("WebSocketConnection::handleFrame() called for fd: %d, opcode: %d", sockfd_, (int)frame.opcode);
    switch (frame.opcode)
    {
    case WebSocketOpCode::TEXT:
    case WebSocketOpCode::BINARY:
    {
        // 将数据交给 ChatService 处理
        bool isText = (frame.opcode == WebSocketOpCode::TEXT);
        // 打印文本内容（如果是文本消息）
        if (isText) {
            std::string textMsg(frame.payload.begin(), frame.payload.end());
            LOG_INFO("Received text message from fd %d: %s", sockfd_, textMsg.c_str());
        }
        // 交给业务层处理（聊天室示例）
        ChatService::instance()->handleMessage(
            this,
            frame.payload.data(),
            frame.payload.size(),
            isText);
        break;
    }
    case WebSocketOpCode::PING:
        // 回复 PONG，同时刷新在线状态和连接超时定时器
        LOG_INFO("Received PING from fd %d, sending PONG", sockfd_);
        OnlineUserManager::instance()->refreshHeartbeat(username_);
        {
            util_timer* t = timer();
            if (t && t->user_data && t->user_data->server) {
                t->user_data->server->adjust_timer(t);
            }
        }
        sendFrame(frame.payload.data(), frame.payload.size(), WebSocketOpCode::PONG);
        break;
    case WebSocketOpCode::PONG:
        // 刷新在线状态和连接超时定时器
        LOG_INFO("Received PONG from fd %d", sockfd_);
        OnlineUserManager::instance()->refreshHeartbeat(username_);
        {
            util_timer* t = timer();
            if (t && t->user_data && t->user_data->server) {
                t->user_data->server->adjust_timer(t);
            }
        } 
        break;
    case WebSocketOpCode::CLOSE:
        // 回复关闭帧并关闭连接
        LOG_INFO("Received CLOSE from fd %d", sockfd_);
        sendClose();
        Close();
        break;
    default:
        // 忽略其他操作码
        LOG_INFO("Received unknown opcode %d from fd %d", (int)frame.opcode, sockfd_);
        break;
    }
}

bool WebSocketConnection::sendFrame(const char* data, size_t len, WebSocketOpCode opcode)
{
    LOG_INFO("WebSocketConnection::sendFrame() called for fd: %d, len: %zu, opcode: %d", sockfd_, len, (int)opcode);
    // 编码为 WebSocket 帧（服务器发送，不掩码）
    std::string frame = EncodeWebSocketFrame(data, len, opcode, false);
    // 将帧添加到写缓冲区
    writeBuffer_.Append(frame.data(), frame.size());
    LOG_INFO("WebSocketConnection::sendFrame() added %zu bytes to write buffer for fd: %d", frame.size(), sockfd_);
    // 注册写事件（如果尚未注册）
    Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLOUT, trigmode_);
    return true;
}

void WebSocketConnection::sendClose(uint16_t code, const std::string& reason)
{
    std::vector<char> payload(2 + reason.size());
    payload[0] = (code >> 8) & 0xFF;
    payload[1] = code & 0xFF;
    std::memcpy(payload.data() + 2, reason.data(), reason.size());
    sendFrame(payload.data(), payload.size(), WebSocketOpCode::CLOSE);
}

void WebSocketConnection::sendData(const char* data, size_t len, WebSocketOpCode opcode)
{
    sendFrame(data, len, opcode);
}