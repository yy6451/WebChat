/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/json_util.h
 * 类型: Header
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
#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <string>
#include <unordered_map>
#include <vector>

class JsonUtil {
public:
    static std::string createResponse(int code, const std::string& msg);
    static std::string createLoginResponse(int code, const std::string& msg, 
                                           const std::string& token, int userId, 
                                           const std::string& username);
    static std::string escapeJson(const std::string& str);
    
    static std::string objectStart();
    static std::string objectEnd();
    static std::string arrayStart();
    static std::string arrayEnd();
    static std::string pair(const std::string& key, const std::string& value);
    static std::string pair(const std::string& key, int value);
    static std::string pair(const std::string& key, bool value);
    static std::string comma();
};

#endif // JSON_UTIL_H
