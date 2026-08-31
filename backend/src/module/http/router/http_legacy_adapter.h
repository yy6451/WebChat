#ifndef MODULE_HTTP_ROUTER_HTTP_LEGACY_ADAPTER_H
#define MODULE_HTTP_ROUTER_HTTP_LEGACY_ADAPTER_H

#include "module/http/http_types.h"

class HttpLegacyAdapter {
public:
    static bool Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out);
};

#endif
