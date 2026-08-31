#include "module/http/service/auth_service.h"

#include "module/http/http_utils.h"
#include "module/http/repository/user_repository.h"
#include "utils/auth/auth_manager.h"
#include "utils/json_util.h"
#include "utils/auth/password_util.h"
#include "utils/sql/sql_connection_pool.h"
#include "core/webserver.h"

#include <sstream>

namespace {

HttpApiResponse MakeJson(int status, const std::string& body)
{
    HttpApiResponse res;
    res.status = status;
    res.statusText = (status == 401) ? "Unauthorized" : "OK";
    res.body = body;
    return res;
}

bool DbEnabled(const HttpDispatchContext& ctx)
{
    return ctx.server && ctx.server->m_enable_db && ctx.server->m_connPool;
}

}  // namespace

HttpApiResponse AuthService::Register(const HttpDispatchContext& ctx)
{
    if (ctx.body.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Missing request body"));
    }

    auto params = http_utils::parseFormData(ctx.body);
    const std::string username = params["username"];
    const std::string password = params["password"];
    const std::string repassword = params["repassword"];

    if (username.empty() || password.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Username and password are required"));
    }
    if (password.length() < 6) {
        return MakeJson(200, JsonUtil::createResponse(1, "Password must be at least 6 characters"));
    }
    if (password != repassword) {
        return MakeJson(200, JsonUtil::createResponse(1, "Passwords do not match"));
    }

    if (!DbEnabled(ctx)) {
        std::string err;
        if (!AuthManager::instance()->registerUser(username, password, err)) {
            return MakeJson(200, JsonUtil::createResponse(1, err.empty() ? "Registration failed" : err));
        }
        return MakeJson(200, JsonUtil::createResponse(0, "Registration successful"));
    }

    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
    if (!mysql) {
        return MakeJson(200, JsonUtil::createResponse(1, "Database connection failed - check if database is running and credentials are correct"));
    }

    std::string err;
    UserRepository repo(mysql);
    const std::string hashedPassword = PasswordUtil::hashPassword(password);
    if (!repo.RegisterUser(username, hashedPassword, err)) {
        return MakeJson(200, JsonUtil::createResponse(1, err));
    }

    // 双写：MySQL 写入成功后同步到 Redis，保持 Redis 缓存一致
    AuthManager::instance()->syncUserPassword(username, hashedPassword, err);

    return MakeJson(200, JsonUtil::createResponse(0, "Registration successful"));
}

HttpApiResponse AuthService::Login(const HttpDispatchContext& ctx)
{
    if (ctx.body.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Missing request body"));
    }

    auto params = http_utils::parseFormData(ctx.body);
    const std::string username = params["username"];
    const std::string password = params["password"];

    if (username.empty() || password.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Username and password are required"));
    }

    if (!DbEnabled(ctx)) {
        std::string err;
        if (!AuthManager::instance()->loginUser(username, password, err)) {
            return MakeJson(200, JsonUtil::createResponse(1, err.empty() ? "Login failed" : err));
        }
        const std::string token = AuthManager::instance()->generateToken(0, username);
        return MakeJson(200, JsonUtil::createLoginResponse(0, "Login successful", token, 0, username));
    }

    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
    if (!mysql) {
        return MakeJson(200, JsonUtil::createResponse(1, "Database connection failed"));
    }

    UserRepository repo(mysql);
    std::string err;
    int userId = 0;
    std::string storedHash;
    if (!repo.FindUserForLogin(username, userId, storedHash, err)) {
        return MakeJson(200, JsonUtil::createResponse(1, err));
    }

    if (!PasswordUtil::verifyPassword(password, storedHash)) {
        return MakeJson(200, JsonUtil::createResponse(1, "Invalid password"));
    }

    // 双写：MySQL 验证通过后同步到 Redis，后续读取走 Redis
    AuthManager::instance()->syncUserPassword(username, storedHash, err);

    const std::string token = AuthManager::instance()->generateToken(userId, username);
    return MakeJson(200, JsonUtil::createLoginResponse(0, "Login successful", token, userId, username));
}

bool AuthService::Authorize(const HttpDispatchContext& ctx, ::UserInfo& info)
{
    std::string token = http_utils::extractBearerToken(ctx.authorization);
    if (token.empty()) {
        token = http_utils::getQueryParam(ctx.url, "token");
    }
    return !token.empty() && AuthManager::instance()->validateToken(token, info);
}

HttpApiResponse AuthService::UserInfo(const HttpDispatchContext& ctx)
{
    ::UserInfo info;
    if (!Authorize(ctx, info)) {
        return MakeJson(401, JsonUtil::createResponse(401, "Unauthorized"));
    }

    std::ostringstream oss;
    oss << "{" 
        << "\"code\":0," 
        << "\"msg\":\"ok\"," 
        << "\"data\":{" 
        << "\"userId\":" << info.id << "," 
        << "\"username\":\"" << info.username << "\"" 
        << "}}";
    return MakeJson(200, oss.str());
}
