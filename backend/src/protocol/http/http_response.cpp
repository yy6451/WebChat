/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/http/http_response.cpp
 * 类型: Source
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
#include "http_response.h"
#include <cstdio>

void HttpResponse::appendToBuffer(std::string* output) const {
    char buf[32];
    const int statusCode = (statusCode_ <= 0) ? 200 : statusCode_;
    const char* statusMessage = statusMessage_.empty() ? "OK" : statusMessage_.c_str();
    snprintf(buf, sizeof(buf), "HTTP/1.1 %d ", statusCode);
    output->append(buf);
    output->append(statusMessage);
    output->append("\r\n");

    if (closeConnection_) {
        output->append("Connection: close\r\n");
    } else {
        output->append("Connection: keep-alive\r\n");
    }

    if (!body_.empty()) {
        snprintf(buf, sizeof(buf), "Content-Length: %zd\r\n", body_.size());
        output->append(buf);
        // 仅在调用方未显式设置时提供默认值，避免重复 Content-Type。
        if (headers_.find("Content-Type") == headers_.end()) {
            output->append("Content-Type: text/html\r\n");
        }
    }

    for (const auto& header : headers_) {
        output->append(header.first);
        output->append(": ");
        output->append(header.second);
        output->append("\r\n");
    }

    output->append("\r\n");

    if (!body_.empty()) {
        output->append(body_);
    }
}