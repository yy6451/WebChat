#ifndef MODULE_HTTP_SERVICE_FRIEND_SERVICE_H
#define MODULE_HTTP_SERVICE_FRIEND_SERVICE_H

#include "module/http/http_types.h"

class FriendService {
public:
    static HttpApiResponse Friends(const HttpDispatchContext& ctx, const UserInfo& info);
    static HttpApiResponse SearchUser(const HttpDispatchContext& ctx, const UserInfo& info);
    static HttpApiResponse AddFriend(const HttpDispatchContext& ctx, const UserInfo& info);
    static HttpApiResponse FriendRequests(const HttpDispatchContext& ctx, const UserInfo& info);
    static HttpApiResponse VerifyFriend(const HttpDispatchContext& ctx, const UserInfo& info);
    static HttpApiResponse ReadFriend(const HttpDispatchContext& ctx, const UserInfo& info);
};

#endif
