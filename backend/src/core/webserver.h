/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/webserver.h
 * 类型: Header
 * 作用: WebServer 核心调度模块：负责 epoll 事件循环、连接接入、信号处理与定时器驱动。
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
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <list>

#include "utils/threadpool/threadpool.h"
#include "connection/connection.h"          // 基类
#include "connection/connection_factory.h"   // 工厂
#include "utils/lock/locker.h"
#include "utils/timer/lst_timer.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    struct WorkerResult {
        int sockfd;
        bool ok;
    };

    WebServer();
    ~WebServer();

    // 初始化运行参数。
    // 该函数只负责把外部配置落到成员变量，不进行网络/线程/数据库资源申请。
    // 资源申请由 log_write/sql_pool/thread_pool/eventListen 分阶段完成。
    void init(int port, std::string user, std::string passWord, std::string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model, std::string doc_root,
              int enable_db, int heartbeat_timeout, std::string mysql_host = "localhost",
              std::string server_ip = "", std::string redis_host = "127.0.0.1",
              int redis_port = 7000, std::string redis_pass = "");

    // 创建工作线程池。
    void thread_pool();
    // 初始化数据库连接池与业务表结构（按配置启用/关闭）。
    void sql_pool();
    // 初始化日志系统（同步/异步由配置决定）。
    void log_write();
    // 解析触发模式配置并设置监听/连接的 LT 或 ET 行为。
    void trig_mode();
    // 创建监听 socket、epoll、信号管道，并注册核心 fd 到 epoll。
    void eventListen();
    // 初始化跨服务器消息总线（ChatMessageBus + OnlineUserManager + MessageDispatcher）
    void initMessageBus();
    // 主事件循环：统一调度 accept/read/write/signal/timer。
    void eventLoop();
    // 新连接接入时：创建连接对象、绑定定时器并加入 epoll。
    void timer(int connfd, struct sockaddr_in client_address);
    // 活跃连接续期：刷新连接超时时间。
    void adjust_timer(util_timer *timer);
    // 连接回收：关闭连接并删除定时器。
    void deal_timer(util_timer *timer, int sockfd);
    // 处理新客户端连接（listenfd 事件）。
    bool dealclientdata();
    // 处理来自信号管道的数据（SIGALRM/SIGTERM）。
    bool dealwithsignal(bool& timeout, bool& stop_server);
    // 处理读事件。
    void dealwithread(int sockfd);
    // 处理写事件。
    void dealwithwrite(int sockfd);
    // 处理 worker 完成通知事件。
    bool dealwithworker();

    // 线程池任务完成回调。
    static void on_worker_done(void *context, int sockfd, bool ok);
    // 把 worker 完成结果入队并唤醒主线程。
    void push_worker_result(int sockfd, bool ok);

public:
    // ===== 基础配置 =====
    int m_port;
    std::string m_doc_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    // 管道用于把异步信号转化为 epoll 可读事件，避免在信号处理函数中执行复杂逻辑。
    int m_pipefd[2];
    // worker 完成通知管道，主线程监听读端。
    int m_worker_pipefd[2];
    // 当前进程的 epoll 实例 fd。
    int m_epollfd;
    // 连接对象表：下标即 socket fd，便于 O(1) 定位连接上下文。
    Connection *users[MAX_FD];

    // ===== 数据库相关 =====
    connection_pool *m_connPool;
    std::string m_user;
    std::string m_passWord;
    std::string m_databaseName;
    int m_sql_num;
    // 是否启用数据库。0=禁用，1=启用。
    int m_enable_db;

    // ===== 线程池相关 =====
    // 线程池任务对象是 Connection（多态，HTTP/WS 共享一套调度）。
    threadpool<Connection> *m_pool;
    int m_thread_num;
    // worker 完成状态队列。
    std::list<WorkerResult> m_worker_results;
    locker m_worker_result_locker;

    // epoll_wait 返回的就绪事件缓存。
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    // SO_LINGER 行为开关。
    int m_OPT_LINGER;
    // 触发模式总配置（0~3）。
    int m_TRIGMode;
    // 监听 socket 的触发模式（LT/ET）。
    int m_LISTENTrigmode;
    // 已连接 socket 的触发模式（LT/ET）。
    int m_CONNTrigmode;
    // 连接心跳超时（秒）。
    int m_heartbeat_timeout;

    // MySQL 主机地址（支持远程连接）
    std::string m_mysql_host;

    // 本服务器对外 IP（跨服务器消息去重用）
    std::string server_announce_ip;

    // Redis 连接参数（给 ChatMessageBus 用）
    std::string m_redis_host;
    int m_redis_port;
    std::string m_redis_pass;

    // ===== 定时器相关 =====
    // 每个 fd 对应一份 timer 上下文，配合升序链表管理超时连接。
    client_data *users_timer;
    // 通用工具：fd 注册、信号处理、定时器驱动等。
    // 已改为使用 Utils::Instance() 单例访问，避免多个实例分散状态。
};

#endif