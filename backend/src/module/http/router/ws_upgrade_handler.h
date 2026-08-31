#ifndef MODULE_HTTP_ROUTER_WS_UPGRADE_HANDLER_H
#define MODULE_HTTP_ROUTER_WS_UPGRADE_HANDLER_H

#include "module/http/http_types.h"

class WsUpgradeHandler {
public:
    static bool Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out);
};

#endif
