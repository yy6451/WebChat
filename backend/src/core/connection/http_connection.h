/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/http_connection.h
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
#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include "connection.h"
#include <map>
#include <string>
#include <sys/uio.h>

#include "../utils/auth/auth_manager.h"
#include "../protocol/http/http_request.h"

class HttpConnection : public Connection {
public:
    /**
     * 构造函数
     * - `sockfd`/`addr`: 连接 socket 与客户端地址
     * - `trigmode`: epoll 触发模式设置（ET/LT）
     * - `close_log`: 日志开关（原代码保留兼容）
     * - `doc_root`: 静态文件根目录，用于文件映射与返回
     * - `server`: 指向 `WebServer` 的指针，用于在 WebSocket 升级后替换连接对象
     */
    HttpConnection(int sockfd, const sockaddr_in& addr, 
                   int trigmode, int close_log, 
                   const char* doc_root,
                   WebServer* server);

    /** 析构：释放 mmap 映射等资源 */
    virtual ~HttpConnection();

    // Connection 多态接口实现
    const char* Type() const override { return "HttpConnection"; }
    virtual bool Read() override;     // 从 socket 读取数据到缓冲区
    virtual bool Write() override;    // 将缓冲区数据写回 socket（支持 writev/mmap 快速发送）
    virtual void Process() override;  // 处理已读数据（解析请求并调度到 Router）
    virtual void Close() override;    // 关闭连接并清理资源

private:
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION, WEBSOCKET_UPGRADE, API_RESPONSE };

    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE do_request();
    void unmap();
    HTTP_CODE buildResponse(int status, const char* status_text, const std::string& content_type, const std::string& body);
    HTTP_CODE buildJsonResponse(int status, const std::string& body);
    HTTP_CODE serveStaticFile(const std::string& resolved_url);

    // ----------------- HTTP 请求 / 响应状态 -----------------
    // 当前请求方法（基于 protocol/http 的枚举）
    HttpRequest::Method method_;
    // 原始请求 URL（未解码）
    std::string url_;
    // 请求体（POST/PUT 的负载）
    std::string body_;
    // 是否保持连接（Keep-Alive）
    bool linger_;

    // ----------------- 文件映射与零拷贝发送 -----------------
    // 文件内存映射地址（当响应为静态文件时使用 mmap）
    char* file_address_;
    // 文件元信息（用于判断权限、大小、是否目录）
    struct stat file_stat_;
    // writev 的 iovec 数组（头部 + 文件数据）
    struct iovec iv_[2];
    int iv_count_;
    // writev 进度跟踪
    int bytes_to_send_;
    int bytes_have_send_;

    // ----------------- 配置 -----------------
    // 静态文件根目录（指向服务器配置的字符串常量）
    const char* doc_root_;
    // epoll 触发模式与日志配置
    int trigmode_;
    int close_log_;

    // ----------------- WebSocket 升级相关 -----------------
    // HTTP 头部中可能出现的 WebSocket Upgrade 字段（临时存储）
    std::string upgrade_;
    std::string connection_;
    std::string sec_websocket_key_;
    std::string sec_websocket_version_;
    // Authorization 头（用于 Bearer token 等认证）
    std::string authorization_;
    // 升级标志：是否是 websocket 连接 & 是否刚刚完成升级握手
    bool is_websocket_;
    bool is_websocket_upgrade_;

    // ----------------- 服务器与请求上下文 -----------------
    // 指向 WebServer 的指针：在完成 WebSocket 握手后会用来替换连接对象
    WebServer* server_;
    // 基于 protocol/http 的请求解析器/上下文
    HttpRequest request_;

    // WebSocket 升级时需要携带的用户信息（由 Router/Service 填充）
    UserInfo userInfo_;
};

#endif