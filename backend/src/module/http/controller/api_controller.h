#ifndef MODULE_HTTP_CONTROLLER_API_CONTROLLER_H
#define MODULE_HTTP_CONTROLLER_API_CONTROLLER_H

#include "module/http/http_types.h"

class ApiController {
public:
    static bool Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out);
};

#endif
