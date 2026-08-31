/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/connection.h
 * 类型: Header
 * 作用: 连接层模块：封装连接生命周期与协议分发（HTTP/WebSocket）。
 * 关键关注点:
 * 1) 对外接口/类声明（或实现入口）与调用约束。
 * 2) 资源管理（内存、fd、锁、数据库连接）与异常路径回收。
 * 3) 并发与线程安全语义（线程池、锁、回调执行上下文）。
 * 4) 与其他模块的数据流边界（协议层/业务层/基础设施层）。
 * 维护建议:
 * 1) 修改接口时同步检查调用方与单元/集成测试。
 * 2) 修改并发逻辑时优先保证可见性与无竞态。
 * 3) 修改协议字段时保持前后端兼容与错误码稳定。
 */
// connection/Connection.h
#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstddef>

#include "../../buffer/buffer.h"          // 自动扩容缓冲区（指向统一实现）
#include "../utils/timer/lst_timer.h"  // 定时器相关（util_timer 定义于此）

// 前向声明 util_timer（已通过 lst_timer.h 引入，此处仅为明确依赖）
class util_timer;

// 抽象基类：所有连接类型（HTTP、WebSocket 等）的公共接口和成员
class Connection {
public:
    // 静态成员：由 WebServer 初始化，供所有连接共享
    static int m_epollfd;      // epoll 实例的文件描述符
    static int m_user_count;   // 当前总连接数

    // 构造函数：初始化连接基本属性
    Connection(int sockfd, const sockaddr_in& addr)
        : sockfd_(sockfd),
          addr_(addr),
          timer_(nullptr) {
        // 缓冲区通过默认构造函数自动初始化
    }

    // 虚析构函数：确保派生类对象正确释放
    virtual ~Connection() = default;

    // ---------- 纯虚接口（必须由派生类实现）----------
    // 获取连接类型（用于日志）
    virtual const char* Type() const = 0;
    // 从 socket 读取数据到读缓冲区（readBuffer_）
    virtual bool Read() = 0;

    // 将写缓冲区（writeBuffer_）的数据写入 socket
    virtual bool Write() = 0;

    // 处理业务逻辑（如 HTTP 请求解析、WebSocket 帧处理）
    virtual void Process() = 0;

    // 关闭连接，释放资源（通常由定时器回调或错误处理调用）
    virtual void Close() = 0;

    int fd() const { return sockfd_; }
    const sockaddr_in& addr() const { return addr_; }

    // 获取/设置定时器指针
    void set_timer(util_timer* timer) { timer_ = timer; }
    util_timer* timer() const { return timer_; }

protected:
    int sockfd_;                 // 连接套接字
    sockaddr_in addr_;           // 客户端地址信息

    Buffer readBuffer_;          // 读缓冲区
    Buffer writeBuffer_;         // 写缓冲区

private:
    util_timer* timer_;          // 指向关联的定时器对象（若存在）
};

#endif // CONNECTION_H