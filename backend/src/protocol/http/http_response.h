/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/http/http_response.h
 * 类型: Header
 * 作用: HTTP 协议模块：负责请求解析、响应构造与静态资源/接口协议适配。
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
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <unordered_map>

class HttpResponse {
public:
    enum HttpStatusCode {
        kUnknown,
        k200Ok = 200,
        k401Unauthorized = 401,
        k400BadRequest = 400,
        k403Forbidden = 403,
        k404NotFound = 404,
        k500InternalError = 500,
    };

    explicit HttpResponse(bool closeConnection = true)
        : statusCode_(kUnknown),
            closeConnection_(closeConnection) {
    }

    void setStatusCode(HttpStatusCode code) { statusCode_ = static_cast<int>(code); }
    void setStatusCode(int code) { statusCode_ = code; }
    void setStatusMessage(const std::string& message) { statusMessage_ = message; }
    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }

    void setContentType(const std::string& contentType) {
        addHeader("Content-Type", contentType);
    }

    void addHeader(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    void setBody(const std::string& body) {
        body_ = body;
    }

    void setBody(const char* data, size_t len) {
        body_.assign(data, len);
    }

    // 将响应内容追加到输出字符串中
    void appendToBuffer(std::string* output) const;

private:
    std::unordered_map<std::string, std::string> headers_;
    int statusCode_;
    std::string statusMessage_;
    bool closeConnection_;
    std::string body_;
};

#endif // HTTP_RESPONSE_H