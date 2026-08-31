#include "http_connection.h"
#include "websocket_connection.h"
#include "../../module/http/http_utils.h"
#include "../utils/log/log.h"
#include "../module/chat/chat_room.h"
#include "../webserver.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef FILENAME_LEN
#define FILENAME_LEN 200
#endif

bool HttpConnection::Read()
{
    int saveErrno = 0;
    ssize_t n = readBuffer_.ReadFd(sockfd_, &saveErrno);
    if (n < 0) {
        return false;
    }
    return true;
}

/**
 * 从 socket 读取数据到 `readBuffer_`。
 * - 非阻塞读取：将错误码通过 `saveErrno` 返回。
 * - 返回值：true 表示读取成功（或无数据但无致命错误），false 表示应关闭连接。
 */

bool HttpConnection::Write()
{
    LOG_INFO("HttpConnection::Write() called for fd: %d, is_websocket_upgrade_=%d", sockfd_, is_websocket_upgrade_);
    int temp = 0;
    if (bytes_to_send_ == 0) {
        Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
        init();
        return true;
    }

    while (true) {
        temp = writev(sockfd_, iv_, iv_count_);
        if (temp < 0) {
            if (errno == EAGAIN) {
                Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLOUT, trigmode_);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;
        if (bytes_have_send_ >= iv_[0].iov_len) {
            iv_[0].iov_len = 0;
            iv_[1].iov_base = file_address_ + (bytes_have_send_ - writeBuffer_.ReadableBytes());
            iv_[1].iov_len = bytes_to_send_;
        } else {
            iv_[0].iov_base = (char*)iv_[0].iov_base + temp;
            iv_[0].iov_len -= temp;
        }

        if (bytes_to_send_ <= 0) {
            unmap();
            if (is_websocket_upgrade_) {
                LOG_INFO("WebSocket handshake response sent, creating WebSocketConnection for fd: %d, username: %s", sockfd_, userInfo_.username.c_str());
                WebSocketConnection* wsConn = new WebSocketConnection(sockfd_, addr_, trigmode_, close_log_, true, userInfo_.username, userInfo_.id);
                if (server_) {
                    server_->users[sockfd_] = wsConn;
                    client_data* cd = &server_->users_timer[sockfd_];
                    if (cd->timer) {
                        cd->conn = wsConn;
                        wsConn->set_timer(cd->timer);
                    }
                }
                Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
                ChatRoom::instance()->Join(wsConn);
                LOG_INFO("WebSocket connection %d joined chat room", sockfd_);
                delete this;
                return true;
            } else if (linger_) {
                Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
                init();
                return true;
            } else {
                Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
                return false;
            }
        }
    }
}

/**
 * 将 `writeBuffer_` 或 mmap 文件通过 `writev` 写回 socket。
 * - 支持分段发送：如果发送未完成会重新注册 EPOLLOUT。
 * - 当发送完成且为 WebSocket 升级路径时，会创建 `WebSocketConnection` 并替换到 `WebServer::users`。
 * - 返回 false 表示应关闭连接或出现不可恢复错误。
 */

void HttpConnection::Process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLIN, trigmode_);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (write_ret) {
        Utils::Instance().modfd(Connection::m_epollfd, sockfd_, EPOLLOUT, trigmode_);
    } else {
        Close();
    }
}

/**
 * 处理一次连接的读取解析与写回调度。
 * - 负责短生命周期的 MYSQL 连接获取（仅在 server_->m_enable_db 时）。
 * - 当 `process_read` 返回 `NO_REQUEST` 时会重新注册 EPOLLIN 并返回。
 * - 否则根据 `process_write` 的结果决定是否注册 EPOLLOUT 或关闭连接。
 */

void HttpConnection::Close()
{
    if (sockfd_ != -1) {
        Utils::Instance().removefd(Connection::m_epollfd, sockfd_);
        sockfd_ = -1;
        Connection::m_user_count--;
    }
    if (file_address_) {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

/**
 * 关闭当前连接并释放可能的文件映射资源。
 * - 会从 epoll 中移除 fd，并将用户计数减1。
 */

HttpConnection::HTTP_CODE HttpConnection::serveStaticFile(const std::string& resolved_url)
{
    if (resolved_url.empty() || resolved_url[0] != '/') {
        return BAD_REQUEST;
    }

    char m_real_file[FILENAME_LEN];
    const int n = snprintf(m_real_file, sizeof(m_real_file), "%s%s", doc_root_, resolved_url.c_str());
    if (n <= 0 || n >= static_cast<int>(sizeof(m_real_file))) {
        return BAD_REQUEST;
    }

    if (stat(m_real_file, &file_stat_) < 0)
        return NO_RESOURCE;

    if (!(file_stat_.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(file_stat_.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    file_address_ = (char *)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    url_ = resolved_url;
    return FILE_REQUEST;
}

/**
 * 解析并尝试打开静态文件路径，使用 `mmap` 映射文件到内存以供零拷贝发送。
 * - 返回 FILE_REQUEST 表示已映射并可由 `process_write` 使用 writev 发送。
 * - 返回 NO_RESOURCE/FORBIDDEN_REQUEST/BAD_REQUEST 表示相应错误。
 */

void HttpConnection::unmap()
{
    if (file_address_) {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = nullptr;
    }
}

/**
 * 解除当前文件的 mmap 映射（如果存在）。
 */
