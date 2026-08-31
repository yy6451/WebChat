#include "module/http/controller/api_controller.h"

#include "module/http/http_utils.h"
#include "module/http/service/auth_service.h"
#include "module/http/service/friend_service.h"

bool ApiController::Handle(const HttpDispatchContext& ctx, HttpDispatchResult& out)
{
    out.type = HttpDispatchResult::kNoMatch;

    if (http_utils::urlPathEquals(ctx.url, "/api/register") && ctx.method == HttpRequest::kPost) {
        out.type = HttpDispatchResult::kApiResponse;
        out.response = AuthService::Register(ctx);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/login") && ctx.method == HttpRequest::kPost) {
        out.type = HttpDispatchResult::kApiResponse;
        out.response = AuthService::Login(ctx);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/userinfo") && ctx.method == HttpRequest::kGet) {
        out.type = HttpDispatchResult::kApiResponse;
        out.response = AuthService::UserInfo(ctx);
        return true;
    }

    UserInfo info;
    if (http_utils::urlPathEquals(ctx.url, "/api/friends") && ctx.method == HttpRequest::kGet) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::Friends(ctx, info);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/searchuser") && ctx.method == HttpRequest::kGet) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::SearchUser(ctx, info);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/addfriend") && ctx.method == HttpRequest::kPost) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::AddFriend(ctx, info);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/friendrequests") && ctx.method == HttpRequest::kGet) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::FriendRequests(ctx, info);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/verifyfriend") && ctx.method == HttpRequest::kPost) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::VerifyFriend(ctx, info);
        return true;
    }

    if (http_utils::urlPathEquals(ctx.url, "/api/readfriend") && ctx.method == HttpRequest::kPost) {
        if (!AuthService::Authorize(ctx, info)) {
            out.type = HttpDispatchResult::kApiResponse;
            out.response.status = 401;
            out.response.statusText = "Unauthorized";
            out.response.body = "{\"code\":401,\"msg\":\"Unauthorized\"}";
            return true;
        }
        out.type = HttpDispatchResult::kApiResponse;
        out.response = FriendService::ReadFriend(ctx, info);
        return true;
    }

    return false;
}
