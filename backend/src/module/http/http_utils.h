#pragma once

#include "../../buffer/buffer.h"
#include <string>
#include <unordered_map>

// ============================================================
// HTTP 辅助工具集合
// ============================================================
// 面向连接、路由与静态资源处理的工具函数。
// 设计原则：无状态、可重入。

// 返回 HTTP 状态码的标准文本描述（例如 200 -> "OK"）
const char* http_status_text_by_code(int status);

// 计算 Sec-WebSocket-Accept（SHA1 + base64）
std::string http_compute_accept(const std::string& key);

// 基于 URL 后缀返回 MIME 类型
const char* http_get_mime_type_by_url(const char* url);

// 将静态文件响应头写入 Buffer
bool http_append_file_headers(Buffer& writeBuffer, size_t fileSize, const std::string& mime, bool keepAlive);

namespace http_utils {

// URL 解码（%XX 与 + 转换为对应字符）
std::string urlDecode(const std::string& str);

// 判断请求路径是否匹配（忽略查询字符串）
bool urlPathEquals(const std::string& url, const char* path);

// 提取 URL 中的 query 参数（不解码）
std::string getQueryParam(const std::string& url, const char* paramName);

// 解析 application/x-www-form-urlencoded body 为键值对
std::unordered_map<std::string, std::string> parseFormData(const std::string& data);

// 从 Authorization 头提取 Bearer token
std::string extractBearerToken(const std::string& authorization);

}  // namespace http_utils
