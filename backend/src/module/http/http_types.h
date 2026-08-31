#ifndef MODULE_HTTP_HTTP_TYPES_H
#define MODULE_HTTP_HTTP_TYPES_H

#include <string>
#include <unordered_map>
#include "protocol/http/http_request.h"
#include "utils/auth/auth_manager.h"

class WebServer;

struct HttpDispatchContext {
    HttpRequest::Method method;
    std::string url;
    std::string body;
    std::string authorization;
    std::string upgrade;
    std::string connection;
    std::string sec_websocket_key;
    WebServer* server;
};

struct HttpApiResponse {
    int status;
    std::string statusText;
    std::string contentType;
    std::string body;

    HttpApiResponse() : status(200), statusText("OK"), contentType("application/json; charset=utf-8") {}
};

struct HttpDispatchResult {
    enum Type {
        kNoMatch,
        kApiResponse,
        kWebSocketUpgrade,
        kRawResponse,
        kStaticFile
    };

    Type type;
    HttpApiResponse response;
    std::string staticUrl;
    std::string rawResponse;
    UserInfo websocketUser;

    HttpDispatchResult() : type(kNoMatch) {}
};

#endif
