#ifndef MODULE_HTTP_REPOSITORY_USER_REPOSITORY_H
#define MODULE_HTTP_REPOSITORY_USER_REPOSITORY_H

#include <mysql/mysql.h>

#include <string>
#include <utility>
#include <vector>

class UserRepository {
public:
    explicit UserRepository(MYSQL* mysql);

    bool RegisterUser(const std::string& username, const std::string& hashedPassword, std::string& err);
    bool FindUserForLogin(const std::string& username, int& userId, std::string& storedHash, std::string& err);
    bool SearchUserExists(const std::string& username, bool& exists, std::string& err);

    bool GetFriends(int userId, std::vector<std::pair<std::string, int>>& friends, std::string& err);
    bool SendFriendRequest(int userId, const std::string& friendName, std::string& err);
    bool ListFriendRequests(int userId, std::vector<std::string>& pendingUsers, std::string& err);
    bool VerifyFriendRequest(int userId, const std::string& fromUser, bool accept, std::string& err);
    bool MarkRead(int userId, const std::string& friendName, std::string& err);
    bool FindUserIdByName(const std::string& username, int& userId, std::string& err);
    bool AreFriends(int fromId, int toId, std::string& err);
    bool SaveMessage(int fromUserId, int toUserId, const std::string& content, std::string& err);

private:
    bool EnsureFriendRequestTable(std::string& err);
    std::string Escape(const std::string& text);

private:
    MYSQL* mysql_;
};

#endif
