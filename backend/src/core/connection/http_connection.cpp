/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/http_connection.cpp
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
#include "http_connection.h"
#include "websocket_connection.h"
#include "../../module/http/router/http_router.h"
#include "../../module/http/http_utils.h"
#include "../protocol/http/http_response.h"
#include "../webserver.h"
#include "../utils/log/log.h"
#include "../module/chat/chat_room.h"
#include "../utils/auth/auth_manager.h"
#include "../utils/auth/password_util.h"
#include "../utils/json_util.h"
#include "../utils/timer/lst_timer.h"
#include <fstream>
#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <cassert>
#include <sstream>
#include <unordered_map>
#include <vector>

// 使用 lst_timer.h 中的工具函数声明（addfd/modfd/removefd/setnonblocking）

// WebSocket and HTTP helper functions moved to module/http/http_utils.{h,cpp}

const int FILENAME_LEN = 200;

// HTTP MySQL 用户映射已移除（预热被懒加载替代）

/**
 * HttpConnection 构造函数
 * -------------------------
 * 说明：创建一个 HTTP 连接实例，负责保存连接级配置并初始化运行态结构。
 * 参数说明：
 *  - `sockfd` : 客户端 socket 描述符（已 accept），由 WebServer 传入。
 *  - `addr`   : 客户端地址信息（sockaddr_in）。
 *  - `trigmode`/`close_log` : epoll 触发模式与日志配置。
 *  - `doc_root` : 静态文件根目录（用于 static file 服务）。
 *  - `server` : 指向包含连接表与配置信息的 `WebServer`，用于升级 WebSocket 时替换连接等操作。
 * 实现细节与不变式：
 *  - 仅做值拷贝与成员初始化，不进行网络 I/O 或阻塞操作。
 *  - 调用 `init()` 进行运行时状态重置（清空缓冲区、标记位等）。
 * 线程安全：构造发生在事件循环/accept 线程，后续并发访问遵循 Connection 的生命周期与 epoll 协议。
 */
HttpConnection::HttpConnection(int sockfd, const sockaddr_in& addr, 
                                 int trigmode, int close_log, 
                                 const char* doc_root,
                                 WebServer* server)
    : Connection(sockfd, addr),
    method_(HttpRequest::kGet),
        linger_(false),
        file_address_(nullptr),
        iv_count_(0),
        bytes_to_send_(0),
        bytes_have_send_(0),
        doc_root_(doc_root),
        trigmode_(trigmode),
        close_log_(close_log),
        server_(server)
{
    // 初始化连接级状态（缓冲区、标志位等）
    init();
}

/**
 * 析构函数
 * ---------
 * 说明：确保释放任何因静态文件服务而创建的资源，尤其是 `mmap` 映射。
 * 注意：在 WebSocket 升级路径中，旧的 `HttpConnection` 可能在替换时被 `delete`，
 * 因此析构应保证无论何种生命周期终止都不会泄漏文件映射或保留未关闭的句柄。
 */
HttpConnection::~HttpConnection()
{
    if (file_address_) {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

/**
 * init
 * ----
 * 作用：重置连接到干净状态，供下一次请求/响应周期使用。
 * 场景：
 *  - 在构造后初始化一次；
 *  - 当 keep-alive 请求处理完成且准备复用连接时调用；
 *  - 在发送完成并保持长连接时用于清空此前请求相关状态。
 * 语义要点：
 *  - 清空读/写缓冲区（避免残余数据干扰新请求解析）；
 *  - 重置与 HTTP 请求相关的临时字段（方法、URL、body、头部缓存等）；
 *  - 清除任何 WebSocket 升级标志以避免错误地进入升级路径。
 */
void HttpConnection::init()
{
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    linger_ = false;
    method_ = HttpRequest::kGet;
    url_.clear();
    body_.clear();

    readBuffer_.RetrieveAll();
    writeBuffer_.RetrieveAll();

    // 清空 WebSocket 相关临时头部信息
    upgrade_.clear();
    request_.reset();
    connection_.clear();
    sec_websocket_key_.clear();
    sec_websocket_version_.clear();
    authorization_.clear();
    is_websocket_ = false;
    is_websocket_upgrade_ = false;
}



/**
 * process_read
 * ------------
 * 说明：从已填充的 `readBuffer_` 中提取数据并驱动 HTTP 解析器 `HttpRequest::parse()`。
 * 返回值：
 *  - `NO_REQUEST` : 数据不完整，需要等待更多字节；
 *  - `BAD_REQUEST`: 解析错误；
 *  - 其他 HTTP_CODE：表示请求已完整并已进入下一步处理（由 `do_request()` 决定具体行为）。
 * 关键步骤：
 *  1. 将读缓冲区现有内容拷贝到临时字符串（解析器会基于该数据填充内部状态）；
 *  2. 调用 `request_.parse()`，根据返回状态做分支：
 *     - 若 `kParseGotAll`：提取 `method/url/body` 以及关键信头部（Upgrade/Connection/Sec-WebSocket-Key/Authorization）
 *       并调用 `do_request()` 继续路由与响应构建；
 *     - 若 `kParseError`：立即返回 `BAD_REQUEST`；
 *     - 否则返回 `NO_REQUEST` 表示需要更多数据。
 * 注意事项：
 *  - 解析器设计为增量安全，因此可以在非阻塞读多次调用；每次 parse 后应正确维护缓冲区边界。
 */
HttpConnection::HTTP_CODE HttpConnection::process_read()
{
    std::string newData(readBuffer_.Peek(), readBuffer_.ReadableBytes());
    readBuffer_.RetrieveAll();
    HttpRequest::ParseState state = request_.parse(newData.c_str(), newData.size());
    if (state == HttpRequest::kParseError) {
        return BAD_REQUEST;
    }
    if (state == HttpRequest::kParseGotAll) {
        method_ = request_.method();
        url_ = request_.url();
        linger_ = request_.keepAlive();
        body_ = request_.body();

        // 保存可能用于 WebSocket 升级或鉴权的头部
        upgrade_ = request_.getHeader("upgrade");
        connection_ = request_.getHeader("connection");
        sec_websocket_key_ = request_.getHeader("sec-websocket-key");
        sec_websocket_version_ = request_.getHeader("sec-websocket-version");
        authorization_ = request_.getHeader("authorization");

        return do_request();
    }
    return NO_REQUEST;
}


/**
 * do_request
 * ----------
 * 说明：将解析后的请求上下文封装为 `HttpDispatchContext` 并交给路由器 `HttpRouter::Dispatch`
 *      来决定如何响应（API、静态文件、直接原始响应或 WebSocket 升级）。
 * 关键点：
 *  - 构建 `HttpDispatchContext` 时会把解析得到的 `method/url/body/authorization` 等信息传递下去；
 *  - `HttpRouter` 返回 `HttpDispatchResult`，包含响应类型与内容；
 *  - 对于 `kApiResponse`：构建标准 HTTP 响应并追加到 `writeBuffer_`（通过 `buildResponse`）；
 *  - 对于 `kWebSocketUpgrade`：计算 `Sec-WebSocket-Accept` 并把 101 握手响应追加到 `writeBuffer_`，同时设置升级标志，后续在写完该缓冲后会把连接替换为 `WebSocketConnection`；
 *  - 对于 `kRawResponse`：直接把 `rawResponse` 字节追加到 `writeBuffer_`（可用于兼容旧版逻辑或代理场景）；
 *  - 对于 `kStaticFile`：交由 `serveStaticFile` 进行文件打开、mmap 和头部构建。
 * 返回值：表示接下来 `process_write` 需要如何处理（如 `API_RESPONSE`、`FILE_REQUEST`、`WEBSOCKET_UPGRADE` 等）。
 */
HttpConnection::HTTP_CODE HttpConnection::do_request()
{
    LOG_INFO("do_request() dispatch: url=%s, method=%d", url_.c_str(), static_cast<int>(method_));

    HttpDispatchContext ctx;
    ctx.method = method_;
    ctx.url = url_;
    ctx.body = body_;
    ctx.authorization = authorization_;
    ctx.upgrade = upgrade_;
    ctx.connection = connection_;
    ctx.sec_websocket_key = sec_websocket_key_;
    ctx.server = server_;

    HttpDispatchResult result = HttpRouter::Dispatch(ctx);
    switch (result.type) {
        case HttpDispatchResult::kApiResponse:
            return buildResponse(result.response.status,
                                 result.response.statusText.c_str(),
                                 result.response.contentType,
                                 result.response.body);
        case HttpDispatchResult::kWebSocketUpgrade: {
            // 保存路由层传回的用户信息（后续创建 WebSocketConnection 时使用）
            userInfo_ = result.websocketUser;
            // 计算 Sec-WebSocket-Accept 并构建 101 握手响应
            std::string accept = http_compute_accept(sec_websocket_key_);
            char response[512];
            snprintf(response, sizeof(response),
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     accept.c_str());
            writeBuffer_.Append(response, strlen(response));
            // 标记为 websocket upgrade，实际替换动作发生在写完成分支
            is_websocket_ = true;
            is_websocket_upgrade_ = true;
            return WEBSOCKET_UPGRADE;
        }
        case HttpDispatchResult::kRawResponse:
            writeBuffer_.Append(result.rawResponse);
            return API_RESPONSE;
        case HttpDispatchResult::kStaticFile:
            return serveStaticFile(result.staticUrl);
        case HttpDispatchResult::kNoMatch:
        default:
            return BAD_REQUEST;
    }
}

/**
 * buildResponse
 * -------------
 * 说明：基于 `HttpApiResponse` 或任意状态/正文构建完整的 HTTP 响应文本并追加到 `writeBuffer_`。
 * 参数：
 *  - `status` : HTTP 状态码（如 200/401/404 等）；
 *  - `status_text` : 可选的状态描述（若为空会从 `http_status_text_by_code` 获取默认文本）；
 *  - `content_type` : 响应 Content-Type（可为空，则不设置）；
 *  - `body` : 响应正文（通常为 JSON 或 HTML）。
 * 注意：函数返回 `API_RESPONSE` 以通知上层写逻辑本次为 API 响应。
 */
HttpConnection::HTTP_CODE HttpConnection::buildResponse(int status, const char* status_text, const std::string& content_type, const std::string& body)
{
    HttpResponse response(!linger_);
    response.setStatusCode(status);
    response.setStatusMessage(status_text ? status_text : http_status_text_by_code(status));
    if (!content_type.empty()) {
        response.setContentType(content_type);
    }
    response.setBody(body);

    std::string raw;
    response.appendToBuffer(&raw);
    writeBuffer_.Append(raw);
    return API_RESPONSE;
}

/**
 * buildJsonResponse
 * -----------------
 * 简单封装：构建 `Content-Type: application/json; charset=utf-8` 的响应并复用 `buildResponse`。
 */
HttpConnection::HTTP_CODE HttpConnection::buildJsonResponse(int status, const std::string& body)
{
    return buildResponse(status, http_status_text_by_code(status), "application/json; charset=utf-8", body);
}


/**
 * process_write
 * -------------
 * 说明：根据 `process_read()` / `do_request()` 返回的 `HTTP_CODE` 构建写入 iovec
 *      并设置 `bytes_to_send_`/`iv_count_` 以供 `Write()` 真正发送。
 * 语义：
 *  - 对错误/未找到/权限等情况使用 `buildResponse` 构造错误页；
 *  - 对静态文件（`FILE_REQUEST`）使用 `mmap` 的文件地址作第二个 iovec，从而实现零拷贝发送（`writev` + `mmap`）；
 *  - 对 WebSocket 升级（`WEBSOCKET_UPGRADE`）情形，握手响应已在 `do_request()` 中追加到 `writeBuffer_`，此处只需设置 iovec 以便后续发送；
 *  - 对 API 响应（`API_RESPONSE`）直接使用 `writeBuffer_` 中的内容作为单个 iovec。
 * 返回值：
 *  - true  表示已成功准备好 iovec 与 `bytes_to_send_`，`Write()` 可继续发送；
 *  - false 表示遇到不可恢复错误，应关闭连接。
 */
bool HttpConnection::process_write(HTTP_CODE ret)
{
    switch (ret) {
        case INTERNAL_ERROR:
            buildResponse(500, http_status_text_by_code(500), "text/plain; charset=utf-8", "There was an unusual problem serving the request file.\n");
            break;
        case NO_RESOURCE:
        case BAD_REQUEST:
            buildResponse(404, http_status_text_by_code(404), "text/plain; charset=utf-8", "The requested file was not found on this server.\n");
            break;
        case FORBIDDEN_REQUEST:
            buildResponse(403, http_status_text_by_code(403), "text/plain; charset=utf-8", "You do not have permission to get file form this server.\n");
            break;
        case FILE_REQUEST: {
            if (file_stat_.st_size != 0) {
                const char* mime = (url_ == "/")
                    ? "text/html; charset=utf-8"
                    : http_get_mime_type_by_url(url_.c_str());
                http_append_file_headers(writeBuffer_, file_stat_.st_size, mime, linger_);
                // iovec[0] => headers, iovec[1] => mmap 文件数据
                iv_[0].iov_base = const_cast<char*>(writeBuffer_.Peek());
                iv_[0].iov_len = writeBuffer_.ReadableBytes();
                iv_[1].iov_base = file_address_;
                iv_[1].iov_len = file_stat_.st_size;
                iv_count_ = 2;
                bytes_to_send_ = writeBuffer_.ReadableBytes() + file_stat_.st_size;
                return true;
            } else {
                const char *ok_string = "<html><body></body></html>";
                http_append_file_headers(writeBuffer_, strlen(ok_string), std::string("text/html; charset=utf-8"), linger_);
                writeBuffer_.Append(ok_string);
            }
            break;
        }
            break;
        case WEBSOCKET_UPGRADE:
            // WebSocket 握手响应已在 do_request 中追加到 writeBuffer_，此处不另行构造
            break;
        case API_RESPONSE:
            // API 响应已经在 do_request 中完全构建好了，直接使用
            break;
        default:
            return false;
    }
    // 默认单 iovec 路径：headers/response body 都在 writeBuffer_ 内
    iv_[0].iov_base = const_cast<char*>(writeBuffer_.Peek());
    iv_[0].iov_len = writeBuffer_.ReadableBytes();
    iv_count_ = 1;
    bytes_to_send_ = writeBuffer_.ReadableBytes();
    return true;
}
