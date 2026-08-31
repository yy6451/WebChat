#include "http_utils.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <sstream>

// ============================================================
// HTTP 辅助工具实现
// ============================================================
// - HTTP 状态码 → 文本映射
// - WebSocket 握手 Accept (SHA1 + base64)
// - MIME 类型推断
// - 静态文件头构建
// - URL / form / Authorization 解析

static const char* statusTextByCodeInternal(int status)
{
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Error";
        default: return "OK";
    }
}

const char* http_status_text_by_code(int status)
{
    return statusTextByCodeInternal(status);
}

// WebSocket 握手魔术字符串常量（RFC6455）
static const std::string WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 计算 Sec-WebSocket-Accept：
// 1) 将客户端 Sec-WebSocket-Key 与 WS_MAGIC 拼接
// 2) 对拼接后的字符串做 SHA1
// 3) 对 SHA1 摘要做 base64 编码
// 返回值可直接放入 Sec-WebSocket-Accept 响应头
std::string http_compute_accept(const std::string& key)
{
    std::string combined = key + WS_MAGIC;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);

    char base64[64];
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(base64), hash, SHA_DIGEST_LENGTH);
    return std::string(base64, len);
}

// 基于 URL 后缀猜测 MIME 类型（覆盖常见前端资源）
const char* http_get_mime_type_by_url(const char* url)
{
    if (!url) return "text/html; charset=utf-8";
    const char* dot = strrchr(url, '.');
    if (!dot) return "text/html; charset=utf-8";

    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(dot, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(dot, ".woff") == 0) return "font/woff";
    if (strcasecmp(dot, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

// 将静态文件响应头写入 writeBuffer（包含 Content-Length/Content-Type/Connection）
bool http_append_file_headers(Buffer& writeBuffer, size_t fileSize, const std::string& mime, bool keepAlive)
{
    std::string headers;
    headers.reserve(256);
    headers += "HTTP/1.1 200 ";
    headers += http_status_text_by_code(200);
    headers += "\r\n";
    headers += "Content-Type:";
    headers += mime.empty() ? "text/html; charset=utf-8" : mime;
    headers += "\r\n";
    headers += "Content-Length:";
    headers += std::to_string(fileSize);
    headers += "\r\n";
    headers += "Connection:";
    headers += (keepAlive ? "keep-alive" : "close");
    headers += "\r\n\r\n";

    writeBuffer.Append(headers);
    return true;
}

namespace http_utils {

// 将 URL encoded 字符串解码为原文（支持 %XX 与 + -> 空格）
// 例如："a%20b+1" -> "a b 1"
std::string urlDecode(const std::string& str) {
    std::string decoded;
    char hex[3] = {0};
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                hex[0] = str[i + 1];
                hex[1] = str[i + 2];
                decoded += static_cast<char>(strtol(hex, nullptr, 16));
                i += 2;
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

// 比较 URL 前缀与给定 path 是否相同（忽略 query string），例如 "/chat?token=xx" 与 "/chat" 相等
bool urlPathEquals(const std::string& url, const char* path) {
    if (!path) {
        return false;
    }
    const size_t pathLen = strlen(path);
    if (url.compare(0, pathLen, path) != 0) {
        return false;
    }
    return url.size() == pathLen || url[pathLen] == '?';
}

// 从 URL 中解析 query 参数，返回未解码的值
// 注意：若需要解码请调用 `urlDecode`。
std::string getQueryParam(const std::string& url, const char* paramName) {
    if (!paramName || !*paramName) {
        return "";
    }
    const size_t qPos = url.find('?');
    if (qPos == std::string::npos || qPos + 1 >= url.size()) {
        return "";
    }

    std::string query = url.substr(qPos + 1);
    std::stringstream ss(query);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        const size_t eqPos = pair.find('=');
        const std::string key = (eqPos == std::string::npos) ? pair : pair.substr(0, eqPos);
        if (key == paramName) {
            return eqPos == std::string::npos ? "" : pair.substr(eqPos + 1);
        }
    }
    return "";
}

// 解析 application/x-www-form-urlencoded 格式的 body（如 "a=1&b=2"），并对 key/value 做 URL 解码
std::unordered_map<std::string, std::string> parseFormData(const std::string& data) {
    std::unordered_map<std::string, std::string> params;
    std::stringstream ss(data);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
        size_t eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eqPos));
            std::string value = urlDecode(pair.substr(eqPos + 1));
            params[key] = value;
        }
    }
    return params;
}

// 从 Authorization 头中抽取 Bearer token（若头以 "Bearer " 开头则返回 token 部分）
// 示例："Bearer AbC123" -> "AbC123"
std::string extractBearerToken(const std::string& authorization) {
    if (authorization.empty()) {
        return "";
    }
    const std::string prefix = "Bearer ";
    if (authorization.rfind(prefix, 0) == 0) {
        return authorization.substr(prefix.size());
    }
    return authorization;
}

}  // namespace http_utils
