/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/protocol/http/http_request.cpp
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
#include "http_request.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace {
std::string ToLowerCopy(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}
}

HttpRequest::HttpRequest()
    : checked_idx_(0),
      start_line_(0),
      state_(kParseRequestLine),
      method_(kInvalid),
      content_length_(0) {
}

void HttpRequest::reset() {
    buffer_.clear();
    checked_idx_ = 0;
    start_line_ = 0;
    state_ = kParseRequestLine;
    method_ = kInvalid;
    url_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    content_length_ = 0;
    path_.clear();
}

HttpRequest::ParseState HttpRequest::parse(const char* data, size_t len) {
    if (len > 0) {
        buffer_.append(data, len);
    }

    // 先解析请求行和头部；正文单独按 Content-Length 判断，避免状态机死循环。
    while (state_ == kParseRequestLine || state_ == kParseHeaders) {
        LineStatus lineStatus = parseLine();
        if (lineStatus == kLineBad) {
            state_ = kParseError;
            return state_;
        }
        if (lineStatus == kLineOpen) {
            return state_;
        }

        const char* lineStart = buffer_.data() + start_line_;
        size_t lineLen = 0;
        if (checked_idx_ >= 2 && buffer_[checked_idx_ - 2] == '\r' && buffer_[checked_idx_ - 1] == '\n') {
            lineLen = checked_idx_ - start_line_ - 2;
        }
        std::string line(lineStart, lineLen);
        start_line_ = checked_idx_;

        if (state_ == kParseRequestLine) {
            if (!parseRequestLine(line.data(), line.size())) {
                state_ = kParseError;
                return state_;
            }
            continue;
        }

        if (!parseHeader(line.data(), line.size())) {
            state_ = kParseError;
            return state_;
        }

        if (line.empty()) {
            state_ = content_length_ > 0 ? kParseBody : kParseGotAll;
            if (state_ == kParseGotAll) {
                return state_;
            }
            break;
        }
    }

    if (state_ == kParseBody) {
        const size_t bodyLen = buffer_.size() - start_line_;
        if (bodyLen < content_length_) {
            return state_;
        }
        body_.assign(buffer_.data() + start_line_, content_length_);
        checked_idx_ = start_line_ + content_length_;
        state_ = kParseGotAll;
    }

    return state_;
}

HttpRequest::LineStatus HttpRequest::parseLine() {
    char temp;
    for (; checked_idx_ < buffer_.size(); ++checked_idx_) {
        temp = buffer_[checked_idx_];
        if (temp == '\r') {
            if ((checked_idx_ + 1) == buffer_.size()) {
                return kLineOpen;
            } else if (buffer_[checked_idx_ + 1] == '\n') {
                checked_idx_ += 2;
                return kLineOk;
            }
            return kLineBad;
        } else if (temp == '\n') {
            if (checked_idx_ > 1 && buffer_[checked_idx_ - 1] == '\r') {
                checked_idx_++;
                return kLineOk;
            }
            return kLineBad;
        }
    }
    return kLineOpen;
}

bool HttpRequest::parseRequestLine(const char* line, size_t len) {
    const char* url = strpbrk(line, " \t");
    if (!url) return false;
    std::string method(line, url - line);
    url += strspn(url, " \t");
    const char* version = strpbrk(url, " \t");
    if (!version) return false;
    std::string urlStr(url, version - url);
    version += strspn(version, " \t");

    if (strcasecmp(method.c_str(), "GET") == 0) method_ = kGet;
    else if (strcasecmp(method.c_str(), "POST") == 0) method_ = kPost;
    else if (strcasecmp(method.c_str(), "HEAD") == 0) method_ = kHead;
    else if (strcasecmp(method.c_str(), "PUT") == 0) method_ = kPut;
    else if (strcasecmp(method.c_str(), "DELETE") == 0) method_ = kDelete;
    else if (strcasecmp(method.c_str(), "TRACE") == 0) method_ = kTrace;
    else if (strcasecmp(method.c_str(), "OPTIONS") == 0) method_ = kOptions;
    else if (strcasecmp(method.c_str(), "CONNECT") == 0) method_ = kConnect;
    else if (strcasecmp(method.c_str(), "PATCH") == 0) method_ = kPatch;
    else return false;

    url_ = urlStr;
    version_ = version;

    // 处理URL中的http://或https://
    if (strncasecmp(url_.c_str(), "http://", 7) == 0) {
        url_ = url_.substr(7);
        size_t pos = url_.find('/');
        url_ = (pos != std::string::npos) ? url_.substr(pos) : "/";
    } else if (strncasecmp(url_.c_str(), "https://", 8) == 0) {
        url_ = url_.substr(8);
        size_t pos = url_.find('/');
        url_ = (pos != std::string::npos) ? url_.substr(pos) : "/";
    }

    if (url_.empty() || url_[0] != '/')
        return false;

    if (url_.size() == 1 && url_[0] == '/') {
        url_ = "/";
    }

    state_ = kParseHeaders;
    return true;
}

bool HttpRequest::parseHeader(const char* line, size_t len) {
    if (len == 0) return true; // 空行

    const char* colon = strchr(line, ':');
    if (!colon) return false;

    std::string key(line, colon - line);
    std::string value(colon + 1);

    // 去除空白
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    };
    trim(key);
    trim(value);
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (key == "connection") {
        headers_["connection"] = value;
    } else if (key == "content-length") {
        content_length_ = atol(value.c_str());
        headers_[key] = value;
    } else {
        headers_[key] = value;
    }
    return true;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(ToLowerCopy(key));
    if (it != headers_.end())
        return it->second;
    return "";
}

bool HttpRequest::keepAlive() const {
    auto it = headers_.find("connection");
    if (it != headers_.end() && strcasecmp(it->second.c_str(), "keep-alive") == 0)
        return true;
    return false;
}