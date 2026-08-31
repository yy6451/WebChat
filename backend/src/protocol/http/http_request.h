/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/http/http_request.h
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
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <unordered_map>

class HttpRequest {
public:
    enum Method {
        kInvalid,
        kGet,
        kPost,
        kHead,
        kPut,
        kDelete,
        kTrace,
        kOptions,
        kConnect,
        kPatch
    };

    enum ParseState {
        kParseRequestLine,
        kParseHeaders,
        kParseBody,
        kParseGotAll,
        kParseError
    };

    enum LineStatus {
        kLineOk,
        kLineBad,
        kLineOpen
    };

    HttpRequest();
    ~HttpRequest() = default;

    // 重置解析状态，用于重新使用
    void reset();

    // 解析输入数据，返回当前状态
    ParseState parse(const char* data, size_t len);

    // 获取解析结果
    Method method() const { return method_; }
    const std::string& url() const { return url_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    std::string getHeader(const std::string& key) const;
    bool keepAlive() const;

    // 设置/获取一些属性（用于后续处理）
    void setPath(const std::string& path) { path_ = path; }
    const std::string& path() const { return path_; }

    // 查询状态
    bool parseSuccess() const { return state_ == kParseGotAll; }
    bool parseError() const { return state_ == kParseError; }

    // 获取已解析的字节数
    size_t parsedBytes() const { return checked_idx_; }

private:
    // 从缓冲区解析一行
    LineStatus parseLine();

    // 解析请求行
    bool parseRequestLine(const char* line, size_t len);

    // 解析头部字段
    bool parseHeader(const char* line, size_t len);

    // 内部缓冲区
    std::string buffer_;
    size_t checked_idx_;   // 已解析的位置
    size_t start_line_;    // 当前行的起始索引

    ParseState state_;
    Method method_;
    std::string url_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    size_t content_length_;   // 从头部解析出的 Content-Length

    // 用于后续业务处理的路径
    std::string path_;
};

#endif // HTTP_REQUEST_H