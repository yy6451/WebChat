#include "user_repository.h"

#include <cstdlib>
#include <sstream>

UserRepository::UserRepository(MYSQL* mysql)
    : mysql_(mysql) {}

std::string UserRepository::Escape(const std::string& text)
{
    if (!mysql_) {
        return "";
    }
    std::string out;
    out.resize(text.size() * 2 + 1);
    unsigned long n = mysql_real_escape_string(mysql_, &out[0], text.c_str(), text.size());
    out.resize(n);
    return out;
}

bool UserRepository::RegisterUser(const std::string& username, const std::string& hashedPassword, std::string& err)
{
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    const std::string escapedName = Escape(username);
    char sqlCheck[512];
    snprintf(sqlCheck, sizeof(sqlCheck), "SELECT username FROM user WHERE username='%s'", escapedName.c_str());
    if (mysql_query(mysql_, sqlCheck) == 0) {
        MYSQL_RES* result = mysql_store_result(mysql_);
        if (result && mysql_num_rows(result) > 0) {
            mysql_free_result(result);
            err = "Username already exists";
            return false;
        }
        if (result) {
            mysql_free_result(result);
        }
    }

    const std::string escapedPassword = Escape(hashedPassword);
    char sqlInsert[512];
    snprintf(sqlInsert, sizeof(sqlInsert), "INSERT INTO user(username, passwd) VALUES('%s', '%s')", escapedName.c_str(), escapedPassword.c_str());
    if (mysql_query(mysql_, sqlInsert) != 0) {
        err = std::string("Registration failed: ") + mysql_error(mysql_);
        return false;
    }
    return true;
}

bool UserRepository::FindUserForLogin(const std::string& username, int& userId, std::string& storedHash, std::string& err)
{
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    const std::string escapedName = Escape(username);
    char sqlSelect[512];
    snprintf(sqlSelect, sizeof(sqlSelect), "SELECT id, passwd FROM user WHERE username='%s'", escapedName.c_str());
    if (mysql_query(mysql_, sqlSelect) != 0) {
        err = "Login failed";
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mysql_);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) {
            mysql_free_result(result);
        }
        err = "User not found";
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    userId = row[0] ? std::atoi(row[0]) : 0;
    storedHash = row[1] ? row[1] : "";
    mysql_free_result(result);
    return true;
}

bool UserRepository::SearchUserExists(const std::string& username, bool& exists, std::string& err)
{
    exists = false;
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    const std::string escaped = Escape(username);
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT id FROM user WHERE username='%s' LIMIT 1", escaped.c_str());
    if (mysql_query(mysql_, sql) != 0) {
        err = "Search user failed";
        return false;
    }

    MYSQL_RES* res = mysql_store_result(mysql_);
    exists = (res && mysql_num_rows(res) > 0);
    if (res) {
        mysql_free_result(res);
    }
    return true;
}

bool UserRepository::GetFriends(int userId, std::vector<std::pair<std::string, int>>& friends, std::string& err)
{
    friends.clear();
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT u.username, "
             "COALESCE(SUM(CASE WHEN m.is_read=0 THEN 1 ELSE 0 END),0) AS unread "
             "FROM friend_relation fr "
             "JOIN user u ON u.id=fr.friend_id "
             "LEFT JOIN message_log m ON m.from_user_id=fr.friend_id AND m.to_user_id=fr.user_id AND m.is_read=0 "
             "WHERE fr.user_id=%d "
             "GROUP BY u.id, u.username", userId);
    if (mysql_query(mysql_, sql) != 0) {
        err = "Query friends failed";
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mysql_);
    if (result) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            std::string name = row[0] ? row[0] : "";
            int unread = row[1] ? std::atoi(row[1]) : 0;
            friends.push_back(std::make_pair(name, unread));
        }
        mysql_free_result(result);
    }
    return true;
}

bool UserRepository::EnsureFriendRequestTable(std::string& err)
{
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    const char* createReqTable =
        "CREATE TABLE IF NOT EXISTS friend_request ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "from_user_id INT NOT NULL,"
        "to_user_id INT NOT NULL,"
        "status TINYINT NOT NULL DEFAULT 0,"
        "create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE KEY uk_friend_req(from_user_id, to_user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    if (mysql_query(mysql_, createReqTable) != 0) {
        err = "Prepare friend request table failed";
        return false;
    }
    return true;
}

bool UserRepository::FindUserIdByName(const std::string& username, int& userId, std::string& err)
{
    userId = 0;
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }

    const std::string escaped = Escape(username);
    char queryUser[512];
    snprintf(queryUser, sizeof(queryUser), "SELECT id FROM user WHERE username='%s'", escaped.c_str());
    if (mysql_query(mysql_, queryUser) != 0) {
        err = "Query friend failed";
        return false;
    }

    MYSQL_RES* res = mysql_store_result(mysql_);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) {
            mysql_free_result(res);
        }
        err = "Friend user not found";
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    userId = row[0] ? std::atoi(row[0]) : 0;
    mysql_free_result(res);
    return true;
}

bool UserRepository::SendFriendRequest(int userId, const std::string& friendName, std::string& err)
{
    if (!EnsureFriendRequestTable(err)) {
        return false;
    }

    int friendId = 0;
    if (!FindUserIdByName(friendName, friendId, err)) {
        return false;
    }
    if (friendId <= 0 || friendId == userId) {
        err = "Invalid friend";
        return false;
    }

    char ins[1024];
    snprintf(ins, sizeof(ins),
             "INSERT INTO friend_request(from_user_id, to_user_id, status) VALUES(%d, %d, 0) "
             "ON DUPLICATE KEY UPDATE status=0, create_time=CURRENT_TIMESTAMP",
             userId, friendId);
    if (mysql_query(mysql_, ins) != 0) {
        err = "Send request failed";
        return false;
    }
    return true;
}

bool UserRepository::ListFriendRequests(int userId, std::vector<std::string>& pendingUsers, std::string& err)
{
    pendingUsers.clear();
    if (!EnsureFriendRequestTable(err)) {
        return false;
    }

    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT u.username FROM friend_request r "
             "JOIN user u ON u.id=r.from_user_id "
             "WHERE r.to_user_id=%d AND r.status=0", userId);
    if (mysql_query(mysql_, sql) != 0) {
        err = "Query requests failed";
        return false;
    }

    MYSQL_RES* result = mysql_store_result(mysql_);
    if (result) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            pendingUsers.push_back(row[0] ? row[0] : "");
        }
        mysql_free_result(result);
    }
    return true;
}

bool UserRepository::VerifyFriendRequest(int userId, const std::string& fromUser, bool accept, std::string& err)
{
    if (!EnsureFriendRequestTable(err)) {
        return false;
    }

    int friendId = 0;
    if (!FindUserIdByName(fromUser, friendId, err)) {
        return false;
    }

    char updReq[512];
    snprintf(updReq, sizeof(updReq),
             "UPDATE friend_request SET status=%d WHERE from_user_id=%d AND to_user_id=%d AND status=0",
             accept ? 1 : 2, friendId, userId);
    if (mysql_query(mysql_, updReq) != 0 || mysql_affected_rows(mysql_) <= 0) {
        err = "Request not found";
        return false;
    }

    if (!accept) {
        return true;
    }

    char ins[1024];
    snprintf(ins, sizeof(ins),
             "INSERT IGNORE INTO friend_relation(user_id, friend_id, status) VALUES(%d,%d,1),(%d,%d,1)",
             userId, friendId, friendId, userId);
    if (mysql_query(mysql_, ins) != 0) {
        err = "Verify failed";
        return false;
    }
    return true;
}

bool UserRepository::MarkRead(int userId, const std::string& friendName, std::string& err)
{
    int friendId = 0;
    if (!FindUserIdByName(friendName, friendId, err)) {
        return false;
    }
    if (friendId <= 0) {
        err = "Invalid friend";
        return false;
    }

    char upd[512];
    snprintf(upd, sizeof(upd),
             "UPDATE message_log SET is_read=1 WHERE from_user_id=%d AND to_user_id=%d AND is_read=0",
             friendId, userId);
    if (mysql_query(mysql_, upd) != 0) {
        err = "Mark read failed";
        return false;
    }
    return true;
}

bool UserRepository::AreFriends(int fromId, int toId, std::string& err)
{
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT 1 FROM friend_relation WHERE user_id=%d AND friend_id=%d AND status=1 LIMIT 1",
             fromId, toId);
    if (mysql_query(mysql_, sql) != 0) {
        err = "Query friend relation failed";
        return false;
    }
    MYSQL_RES* res = mysql_store_result(mysql_);
    bool ok = (res && mysql_num_rows(res) > 0);
    if (res) mysql_free_result(res);
    return ok;
}

bool UserRepository::SaveMessage(int fromUserId, int toUserId, const std::string& content, std::string& err)
{
    if (!mysql_) {
        err = "Database connection failed";
        return false;
    }
    const std::string escaped = Escape(content);
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO message_log (from_user_id, to_user_id, msg_type, content, is_read) "
             "VALUES (%d, %d, 'private', '%s', 0)",
             fromUserId, toUserId, escaped.c_str());
    if (mysql_query(mysql_, sql) != 0) {
        err = std::string("Insert message failed: ") + mysql_error(mysql_);
        return false;
    }
    return true;
}
