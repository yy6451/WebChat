/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/password_util.h
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
#ifndef PASSWORD_UTIL_H
#define PASSWORD_UTIL_H

#include <string>

class PasswordUtil {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& hash);

private:
    static std::string generateSalt();
    static std::string pbkdf2Sha256(const std::string& password, const std::string& salt, int iterations);
    static std::string toHex(const unsigned char* data, size_t len);
    static void fromHex(const std::string& hex, unsigned char* data, size_t len);
};

#endif // PASSWORD_UTIL_H
