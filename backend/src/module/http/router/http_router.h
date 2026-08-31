#ifndef MODULE_HTTP_ROUTER_HTTP_ROUTER_H
#define MODULE_HTTP_ROUTER_HTTP_ROUTER_H

#include "module/http/http_types.h"

class HttpRouter {
public:
    static HttpDispatchResult Dispatch(const HttpDispatchContext& ctx);
};

#endif
