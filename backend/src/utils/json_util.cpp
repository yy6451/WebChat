/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/json_util.cpp
 * 类型: Source
 * 作用: 鉴权模块：负责用户认证、令牌管理与登录态校验。
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
#include "json_util.h"
#include <sstream>
#include <iomanip>

std::string JsonUtil::escapeJson(const std::string& str) {
    std::ostringstream o;
    for (auto c : str) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u"
                      << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string JsonUtil::objectStart() {
    return "{";
}

std::string JsonUtil::objectEnd() {
    return "}";
}

std::string JsonUtil::arrayStart() {
    return "[";
}

std::string JsonUtil::arrayEnd() {
    return "]";
}

std::string JsonUtil::pair(const std::string& key, const std::string& value) {
    return "\"" + escapeJson(key) + "\":\"" + escapeJson(value) + "\"";
}

std::string JsonUtil::pair(const std::string& key, int value) {
    return "\"" + escapeJson(key) + "\":" + std::to_string(value);
}

std::string JsonUtil::pair(const std::string& key, bool value) {
    return "\"" + escapeJson(key) + "\":" + std::string(value ? "true" : "false");
}

std::string JsonUtil::comma() {
    return ",";
}

std::string JsonUtil::createResponse(int code, const std::string& msg) {
    std::ostringstream ss;
    ss << objectStart()
       << pair("code", code) << comma()
       << pair("msg", msg)
       << objectEnd();
    return ss.str();
}

std::string JsonUtil::createLoginResponse(int code, const std::string& msg, 
                                           const std::string& token, int userId, 
                                           const std::string& username) {
    std::ostringstream ss;
    ss << objectStart()
       << pair("code", code) << comma()
       << pair("msg", msg);
    
    if (code == 0) {
        ss << comma()
           << "\"data\":" << objectStart()
           << pair("token", token) << comma()
           << pair("userId", userId) << comma()
           << pair("username", username)
           << objectEnd();
    }
    
    ss << objectEnd();
    return ss.str();
}
