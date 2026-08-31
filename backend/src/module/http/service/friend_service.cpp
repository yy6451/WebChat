#include "module/http/service/friend_service.h"

#include "module/http/service/auth_service.h"
#include "module/http/http_utils.h"
#include "module/http/repository/user_repository.h"
#include "utils/auth/auth_manager.h"
#include "utils/json_util.h"
#include "utils/sql/sql_connection_pool.h"
#include "core/webserver.h"

#include <sstream>
#include <vector>

namespace {

HttpApiResponse MakeJson(int status, const std::string& body)
{
    HttpApiResponse res;
    res.status = status;
    res.statusText = (status == 401) ? "Unauthorized" : "OK";
    res.body = body;
    return res;
}

bool DbEnabled(const HttpDispatchContext& ctx, const UserInfo& info)
{
    return ctx.server && ctx.server->m_enable_db && ctx.server->m_connPool && info.id > 0;
}

}  // namespace

HttpApiResponse FriendService::Friends(const HttpDispatchContext& ctx, const UserInfo& info)
{
    std::string jsonResp;
    if (DbEnabled(ctx, info)) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            std::vector<std::pair<std::string, int>> friends;
            std::string err;
            if (!repo.GetFriends(info.id, friends, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                std::ostringstream oss;
                oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":[";
                for (size_t i = 0; i < friends.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << "{" << "\"username\":\"" << friends[i].first << "\"," << "\"unread\":" << friends[i].second << "}";
                }
                oss << "]}";
                jsonResp = oss.str();
            }
        }
    } else {
        std::vector<std::string> users = AuthManager::instance()->listFriends(info.username);
        std::ostringstream oss;
        oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":[";
        for (size_t i = 0; i < users.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{" << "\"username\":\"" << users[i] << "\"," << "\"unread\":0}";
        }
        oss << "]}";
        jsonResp = oss.str();
    }
    return MakeJson(200, jsonResp);
}

HttpApiResponse FriendService::SearchUser(const HttpDispatchContext& ctx, const UserInfo& info)
{
    (void)info;
    const std::string username = http_utils::urlDecode(http_utils::getQueryParam(ctx.url, "username"));
    if (username.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "username is required"));
    }

    std::string jsonResp;
    if (ctx.server && ctx.server->m_enable_db && ctx.server->m_connPool) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            bool exists = false;
            std::string err;
            if (!repo.SearchUserExists(username, exists, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                std::ostringstream oss;
                oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":{"
                    << "\"username\":\"" << username << "\"," << "\"exists\":" << (exists ? "true" : "false")
                    << "}}";
                jsonResp = oss.str();
            }
        }
    } else {
        bool exists = AuthManager::instance()->hasKnownUsername(username);
        std::ostringstream oss;
        oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":{"
            << "\"username\":\"" << username << "\"," << "\"exists\":" << (exists ? "true" : "false") << "}}";
        jsonResp = oss.str();
    }
    return MakeJson(200, jsonResp);
}

HttpApiResponse FriendService::AddFriend(const HttpDispatchContext& ctx, const UserInfo& info)
{
    if (ctx.body.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Missing request body"));
    }

    auto params = http_utils::parseFormData(ctx.body);
    const std::string friendName = params["friend"];
    if (friendName.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "friend is required"));
    }

    std::string jsonResp;
    if (DbEnabled(ctx, info)) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            std::string err;
            if (!repo.SendFriendRequest(info.id, friendName, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                // 双写：MySQL 写入成功后同步到 Redis
                AuthManager::instance()->syncFriendRequest(info.username, friendName, err);
                jsonResp = JsonUtil::createResponse(0, "Friend request sent");
            }
        }
    } else {
        std::string err;
        if (AuthManager::instance()->sendFriendRequest(info.username, friendName, err)) {
            jsonResp = JsonUtil::createResponse(0, "Friend request sent");
        } else {
            jsonResp = JsonUtil::createResponse(1, err.empty() ? "Add friend failed" : err);
        }
    }
    return MakeJson(200, jsonResp);
}

HttpApiResponse FriendService::FriendRequests(const HttpDispatchContext& ctx, const UserInfo& info)
{
    std::string jsonResp;
    if (DbEnabled(ctx, info)) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            std::vector<std::string> pending;
            std::string err;
            if (!repo.ListFriendRequests(info.id, pending, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                std::ostringstream oss;
                oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":[";
                for (size_t i = 0; i < pending.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << "\"" << pending[i] << "\"";
                }
                oss << "]}";
                jsonResp = oss.str();
            }
        }
    } else {
        auto pending = AuthManager::instance()->listPendingRequests(info.username);
        std::ostringstream oss;
        oss << "{" << "\"code\":0," << "\"msg\":\"ok\"," << "\"data\":[";
        for (size_t i = 0; i < pending.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << pending[i] << "\"";
        }
        oss << "]}";
        jsonResp = oss.str();
    }
    return MakeJson(200, jsonResp);
}

HttpApiResponse FriendService::VerifyFriend(const HttpDispatchContext& ctx, const UserInfo& info)
{
    if (ctx.body.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Missing request body"));
    }

    auto params = http_utils::parseFormData(ctx.body);
    const std::string fromUser = params["friend"];
    const std::string action = params["action"];
    const bool accept = (action == "accept");
    if (fromUser.empty() || (action != "accept" && action != "reject")) {
        return MakeJson(200, JsonUtil::createResponse(1, "friend and action(accept|reject) are required"));
    }

    std::string jsonResp;
    if (DbEnabled(ctx, info)) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            std::string err;
            if (!repo.VerifyFriendRequest(info.id, fromUser, accept, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                // 双写：MySQL 写入成功后同步到 Redis
                if (accept) {
                    AuthManager::instance()->syncFriendRelation(info.username, fromUser, err);
                }
                AuthManager::instance()->removeFriendRequest(info.username, fromUser);
                jsonResp = JsonUtil::createResponse(0, accept ? "Friend request accepted" : "Friend request rejected");
            }
        }
    } else {
        std::string err;
        if (AuthManager::instance()->verifyFriendRequest(info.username, fromUser, accept, err)) {
            jsonResp = JsonUtil::createResponse(0, accept ? "Friend request accepted" : "Friend request rejected");
        } else {
            jsonResp = JsonUtil::createResponse(1, err.empty() ? "Verify failed" : err);
        }
    }
    return MakeJson(200, jsonResp);
}

HttpApiResponse FriendService::ReadFriend(const HttpDispatchContext& ctx, const UserInfo& info)
{
    if (ctx.body.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "Missing request body"));
    }

    auto params = http_utils::parseFormData(ctx.body);
    const std::string friendName = params["friend"];
    if (friendName.empty()) {
        return MakeJson(200, JsonUtil::createResponse(1, "friend is required"));
    }

    std::string jsonResp;
    if (DbEnabled(ctx, info)) {
        MYSQL* mysql = nullptr;
        connectionRAII mysqlcon(&mysql, ctx.server->m_connPool);
        if (!mysql) {
            jsonResp = JsonUtil::createResponse(1, "Database connection failed");
        } else {
            UserRepository repo(mysql);
            std::string err;
            if (!repo.MarkRead(info.id, friendName, err)) {
                jsonResp = JsonUtil::createResponse(1, err);
            } else {
                jsonResp = JsonUtil::createResponse(0, "Read marked");
            }
        }
    } else {
        jsonResp = JsonUtil::createResponse(0, "Read marked");
    }

    return MakeJson(200, jsonResp);
}
