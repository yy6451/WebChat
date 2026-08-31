/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/auth/password_util.cpp
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
#include "password_util.h"
#include "../log/log.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <cstring>

const int SALT_LENGTH = 16;
const int HASH_LENGTH = 32;
const int ITERATIONS = 10000;

std::string PasswordUtil::toHex(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return ss.str();
}

void PasswordUtil::fromHex(const std::string& hex, unsigned char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        std::string byteStr = hex.substr(i * 2, 2);
        data[i] = static_cast<unsigned char>(strtol(byteStr.c_str(), nullptr, 16));
    }
}

std::string PasswordUtil::generateSalt() {
    unsigned char salt[SALT_LENGTH];
    RAND_bytes(salt, SALT_LENGTH);
    return toHex(salt, SALT_LENGTH);
}

std::string PasswordUtil::pbkdf2Sha256(const std::string& password, const std::string& salt, int iterations) {
    unsigned char saltBytes[SALT_LENGTH];
    fromHex(salt, saltBytes, SALT_LENGTH);

    unsigned char hash[HASH_LENGTH];
    PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                       saltBytes, SALT_LENGTH,
                       iterations,
                       EVP_sha256(),
                       HASH_LENGTH, hash);

    return toHex(hash, HASH_LENGTH);
}

std::string PasswordUtil::hashPassword(const std::string& password) {
    std::string salt = generateSalt();
    std::string hash = pbkdf2Sha256(password, salt, ITERATIONS);
    
    std::stringstream ss;
    ss << ITERATIONS << "$" << salt << "$" << hash;
    return ss.str();
}

bool PasswordUtil::verifyPassword(const std::string& password, const std::string& hash) {
    size_t firstDollar = hash.find('$');
    size_t secondDollar = hash.find('$', firstDollar + 1);
    
    if (firstDollar == std::string::npos || secondDollar == std::string::npos) {
        LOG_ERROR("Invalid hash format");
        return false;
    }

    int iterations = std::stoi(hash.substr(0, firstDollar));
    std::string salt = hash.substr(firstDollar + 1, secondDollar - firstDollar - 1);
    std::string storedHash = hash.substr(secondDollar + 1);

    std::string computedHash = pbkdf2Sha256(password, salt, iterations);
    return storedHash == computedHash;
}
