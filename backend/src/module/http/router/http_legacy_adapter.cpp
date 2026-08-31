#include "module/http/router/http_legacy_adapter.h"

#include "module/http/http_utils.h"

#include <string>

namespace {
std::string NormalizePath(const std::string& url)
{
    const size_t qPos = url.find('?');
    if (qPos == std::string::npos) {
        return url;
    }
    return url.substr(0, qPos);
}

bool IsAssetPath(const std::string& path)
{
    if (path == "/favicon.ico") {
        return true;
    }
    if (path.rfind("/assets/", 0) == 0) {
        return true;
    }
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}
}

bool HttpLegacyAdapter::Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out)
{
    const std::string path = NormalizePath(ctx.url);

    if (ctx.method != HttpRequest::kGet && ctx.method != HttpRequest::kHead) {
        out.type = HttpDispatchResult::kApiResponse;
        out.response.status = 405;
        out.response.statusText = "Method Not Allowed";
        out.response.contentType = "text/plain; charset=utf-8";
        out.response.body = "Method Not Allowed";
        return true;
    }

    out.type = HttpDispatchResult::kStaticFile;
    if (path == "/") {
        out.staticUrl = "/index.html";
        return true;
    }

    if (IsAssetPath(path)) {
        out.staticUrl = path;
        return true;
    }

    out.staticUrl = "/index.html";
    return true;
}
