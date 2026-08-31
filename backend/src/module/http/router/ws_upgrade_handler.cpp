#include "module/http/router/ws_upgrade_handler.h"

#include "module/http/http_utils.h"
#include "utils/auth/auth_manager.h"
#include "utils/json_util.h"
#include "core/webserver.h"

#include <strings.h>

bool WsUpgradeHandler::Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out)
{
    out.type = HttpDispatchResult::kNoMatch;

    const bool isUpgradeWebSocket = (strcasecmp(ctx.upgrade.c_str(), "websocket") == 0);
    const bool isConnectionUpgrade = (ctx.connection.find("Upgrade") != std::string::npos ||
                                      ctx.connection.find("upgrade") != std::string::npos);
    if (!http_utils::urlPathEquals(ctx.url, "/chat") || !isUpgradeWebSocket || !isConnectionUpgrade) {
        return false;
    }

    std::string token = http_utils::getQueryParam(ctx.url, "token");
    std::string user = http_utils::getQueryParam(ctx.url, "user");
    if (!user.empty()) {
        user = http_utils::urlDecode(user);
    }

    UserInfo userInfo;
    bool pass = false;
    if (!token.empty() && AuthManager::instance()->validateToken(token, userInfo)) {
        pass = true;
    } else if (ctx.server && ctx.server->m_enable_db) {
        pass = false;
    } else {
        pass = !user.empty();
        if (pass) {
            userInfo.id = 0;
            userInfo.username = user;
        }
    }

    if (pass) {
        out.type = HttpDispatchResult::kWebSocketUpgrade;
        out.websocketUser = userInfo;
        return true;
    }

    out.type = HttpDispatchResult::kApiResponse;
    out.response.status = 401;
    out.response.statusText = "Unauthorized";
    out.response.body = JsonUtil::createResponse(1, "Invalid or missing token");
    return true;
}
