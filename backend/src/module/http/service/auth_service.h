#ifndef MODULE_HTTP_SERVICE_AUTH_SERVICE_H
#define MODULE_HTTP_SERVICE_AUTH_SERVICE_H

#include "module/http/http_types.h"

class AuthService {
public:
    static HttpApiResponse Register(const HttpDispatchContext& ctx);
    static HttpApiResponse Login(const HttpDispatchContext& ctx);
    static bool Authorize(const HttpDispatchContext& ctx, ::UserInfo& info);
    static HttpApiResponse UserInfo(const HttpDispatchContext& ctx);
};

#endif
