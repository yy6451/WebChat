#include "module/http/router/http_router.h"

#include "module/http/controller/api_controller.h"
#include "module/http/router/http_legacy_adapter.h"
#include "module/http/router/ws_upgrade_handler.h"

HttpDispatchResult HttpRouter::Dispatch(const HttpDispatchContext& ctx)
{
    HttpDispatchResult out;
    if (ApiController::Handle(ctx, out)) {
        return out;
    }

    if (WsUpgradeHandler::Handle(ctx, out)) {
        return out;
    }

    HttpLegacyAdapter::Handle(ctx, out);
    return out;
}
