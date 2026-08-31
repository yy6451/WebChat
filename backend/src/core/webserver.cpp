/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/webserver.cpp
 * 类型: Source
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
#include "webserver.h"
#include "connection/connection_factory.h"
#include "connection/http_connection.h"
#include "module/chat/chat_service.h"
#include "module/chat/chat_message_bus.h"
#include "module/chat/message_dispatcher.h"
#include "module/chat/online_user_manager.h"
#include "module/chat/chat_room.h"
#include "utils/log/log.h"
#include "utils/timer/lst_timer.h"
#include "utils/threadpool/threadpool.h"
#include "utils/sql/sql_connection_pool.h"
#include "utils/auth/auth_manager.h"
#include <cstring>
#include <cassert>
#include <signal.h>
#include <sys/stat.h>

// Connection 静态成员定义：
// m_epollfd 在 eventListen 中初始化后被所有连接对象共享；
// m_user_count 用于快速限制最大并发连接数。
int Connection::m_epollfd = -1;
int Connection::m_user_count = 0;

// 低层 FD/epoll 操作由 Utils 提供实现；原先全局 extern 声明已移除，
// 该文件保留薄包装以兼容调用点。

// 以前的全局兼容包装已移除；请统一使用 `Utils::Instance()`。

// 定义 Connection 的静态成员已在 Connection.h 中

WebServer::WebServer()
{
    // 连接表初始化为空指针，避免析构阶段误删野指针。
    for (int i = 0; i < MAX_FD; ++i) {
        users[i] = nullptr;
    }

    // 为每个可能的 fd 预分配定时器上下文，降低运行期碎片化。
    users_timer = new client_data[MAX_FD];

    m_worker_pipefd[0] = -1;
    m_worker_pipefd[1] = -1;
}

WebServer::~WebServer()
{
    // 按“监听/epoll/管道 -> 连接对象 -> 辅助资源”的顺序回收，避免悬挂引用。
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    if (m_worker_pipefd[0] != -1) {
        close(m_worker_pipefd[0]);
    }
    if (m_worker_pipefd[1] != -1) {
        close(m_worker_pipefd[1]);
    }

    // 连接对象由 WebServer 统一持有，逐一 delete。
    for (int i = 0; i < MAX_FD; ++i) {
        if (users[i] != nullptr) {
            delete users[i];
        }
    }

    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName,
                     int log_write, int opt_linger, int trigmode, int sql_num,
                     int thread_num, int close_log, int actor_model, string doc_root,
                     int enable_db, int heartbeat_timeout, string mysql_host,
                     string server_ip, string redis_host, int redis_port, string redis_pass)
{
    // 仅写入配置，不创建系统资源。
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
    m_doc_root = doc_root;
    m_enable_db = enable_db;
    m_heartbeat_timeout = heartbeat_timeout > 0 ? heartbeat_timeout : 60;
    m_mysql_host = mysql_host;
    server_announce_ip = server_ip;
    m_redis_host = redis_host;
    m_redis_port = redis_port;
    m_redis_pass = redis_pass;
}

void WebServer::trig_mode()
{
    // 把配置项 m_TRIGMode 映射为监听 fd 与连接 fd 的组合模式。
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 日志目录不存在时自动创建。
        mkdir("./logs", 0755);
        // 初始化日志
        bool log_init_success;
        if (1 == m_log_write)
            log_init_success = Log::get_instance()->init("./logs/ServerLog", m_close_log, 2000, 800000, 800);
        else
            log_init_success = Log::get_instance()->init("./logs/ServerLog", m_close_log, 2000, 800000, 0);
        
        if (!log_init_success) {
            fprintf(stderr, "Log initialization failed - logging disabled\n");
            m_close_log = 1;
        }
    }
}

void WebServer::sql_pool()
{
    if (!m_enable_db) {
        // 未启用数据库时，ChatService 以无 DB 模式运行。
        LOG_INFO("Database mode disabled");
        m_connPool = nullptr;
        ChatService::instance(nullptr);
        return;
    }

    // 初始化数据库连接池，后续业务 SQL 均从该池借还连接。
    m_connPool = connection_pool::GetInstance();
    if (!m_connPool->init(m_mysql_host, m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log)) {
        LOG_WARN("Database connection pool initialization failed - database features disabled");
    } else {
        // 建表已统一由 database_init.sql 管理

        LOG_INFO("Database pool ready, Redis cache populated on demand");

        // 初始化聊天服务单例，使其具备 DB 能力。
        ChatService::instance(m_connPool);
    }
}

void WebServer::thread_pool()
{
    // 线程池消费 Connection 任务，实际处理逻辑由多态 Process/Read/Write 决定。
    threadpool<Connection>::TaskDoneCallback doneCb = nullptr;
    void *doneCtx = nullptr;
    if (m_actormodel == 1) {
        doneCb = &WebServer::on_worker_done;
        doneCtx = this;
    }
    m_pool = new threadpool<Connection>(m_actormodel, m_connPool, doneCb, doneCtx, m_thread_num);
}

void WebServer::eventListen()
{
    // 1) 创建监听 socket。
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 2) 配置 SO_LINGER，控制 close() 行为（立即返回/等待发送队列）。
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 3) 绑定地址并开始监听。
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0) {
        perror("bind failed");
        exit(1);
    }
    LOG_INFO("Bind successful to port %d", m_port);
    ret = listen(m_listenfd, 5);
    if (ret < 0) {
        perror("listen failed");
        exit(1);
    }
    LOG_INFO("Listen successful on port %d", m_port);

    Utils::Instance().init(TIMESLOT, m_heartbeat_timeout);

    // 4) 创建 epoll 实例。
    m_epollfd = epoll_create(5);
    if (m_epollfd == -1) {
        perror("epoll_create failed");
        exit(1);
    }

    // 5) 广播 epoll fd 到连接层，便于连接对象内部进行 modfd/removefd。
    Connection::m_epollfd = m_epollfd;

    // 6) 注册监听 fd。
    Utils::Instance().addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    LOG_INFO("addfd successful for listenfd");

    // 7) 使用 socketpair 接收异步信号，统一并入 epoll 事件循环。
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    if (ret == -1) {
        perror("socketpair failed");
        exit(1);
    }
    Utils::Instance().setnonblocking(m_pipefd[1]);
    Utils::Instance().addfd(m_epollfd, m_pipefd[0], false, 0);

    // 9) worker 完成通知管道：用于 Reactor 模式把 worker 执行结果回传主线程回收。
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_worker_pipefd);
    if (ret == -1) {
        perror("worker socketpair failed");
        exit(1);
    }
    Utils::Instance().setnonblocking(m_worker_pipefd[0]);
    Utils::Instance().setnonblocking(m_worker_pipefd[1]);
    Utils::Instance().addfd(m_epollfd, m_worker_pipefd[0], false, 0);

    // 10) 安装信号处理：
    // SIGPIPE 忽略防止向已关闭 socket 写入导致进程退出；
    // SIGALRM 用于周期性定时器驱动；SIGTERM 用于优雅退出。
    Utils::Instance().addsig(SIGPIPE, SIG_IGN);
    Utils::Instance().addsig(SIGALRM, Utils::Instance().sig_handler, false);
    Utils::Instance().addsig(SIGTERM, Utils::Instance().sig_handler, false);

    // ========== 初始化跨服务器消息模块 ==========
    initMessageBus();

    // 启动周期闹钟。
    alarm(TIMESLOT);

    // 将关键 fd 写入 Utils 静态成员，供回调使用。
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

    LOG_INFO("Server started on port %d", m_port);
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 1) 通过工厂按协议创建连接对象（HTTP/WS 或其他扩展）。
    users[connfd] = ConnectionFactory::Create(
        connfd, client_address,
        m_CONNTrigmode, m_close_log,
        m_doc_root.c_str(),
        this
    );
    if (users[connfd] == nullptr) {
        LOG_ERROR("Failed to create connection for fd %d", connfd);
        close(connfd);
        return;
    }

    // 2) 填充 client_data，作为定时器回调的上下文载体。
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    users_timer[connfd].conn = users[connfd];
    users_timer[connfd].server = this;   // 关键：设置 server 指针

    // 3) 创建定时器并绑定连接。
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + m_heartbeat_timeout;
    users_timer[connfd].timer = timer;

    // 4) 注册到连接对象与全局定时器链表。
    users[connfd]->set_timer(timer);
    Utils::Instance().m_timer_wheel.add_timer(timer);

    // 5) 将连接 fd 加入 epoll（连接 fd 使用 one-shot 防止并发处理）。
    Utils::Instance().addfd(m_epollfd, connfd, true, m_CONNTrigmode);
}

void WebServer::on_worker_done(void *context, int sockfd, bool ok)
{
    if (!context) {
        return;
    }
    WebServer *server = static_cast<WebServer *>(context);
    server->push_worker_result(sockfd, ok);
}

void WebServer::push_worker_result(int sockfd, bool ok)
{
    WorkerResult result;
    result.sockfd = sockfd;
    result.ok = ok;

    m_worker_result_locker.lock();
    m_worker_results.push_back(result);
    m_worker_result_locker.unlock();

    char notify = '1';
    ssize_t n = send(m_worker_pipefd[1], &notify, 1, 0);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_WARN("worker notify send failed, errno=%d", errno);
    }
}

bool WebServer::dealwithworker()
{
    char buf[1024];
    while (true) {
        ssize_t n = recv(m_worker_pipefd[0], buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        if (n == 0) {
            break;
        }
    }

    std::list<WorkerResult> localResults;
    m_worker_result_locker.lock();
    localResults.swap(m_worker_results);
    m_worker_result_locker.unlock();

    for (std::list<WorkerResult>::iterator it = localResults.begin(); it != localResults.end(); ++it) {
        int sockfd = it->sockfd;
        if (sockfd < 0 || sockfd >= MAX_FD) {
            continue;
        }
        if (users[sockfd] == nullptr) {
            continue;
        }

        util_timer *timer = users_timer[sockfd].timer;
        if (!it->ok) {
            deal_timer(timer, sockfd);
        } else if (timer) {
            adjust_timer(timer);
        }
    }

    return true;
}

void WebServer::adjust_timer(util_timer *timer)
{
    // 活跃连接续期：每次读/写成功后刷新过期时间。
    time_t cur = time(NULL);
    timer->expire = cur + m_heartbeat_timeout;
    Utils::Instance().m_timer_wheel.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    // 统一连接回收入口：
    // Close() 负责 fd 与协议层清理；随后删除对象并解除定时器。
    if (users[sockfd] != nullptr) {
        users[sockfd]->Close();
        delete users[sockfd];
        users[sockfd] = nullptr;
    }
    if (timer) {
        Utils::Instance().m_timer_wheel.del_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata()
{
    // LT: 一次处理一个 accept；ET: 循环 accept 直到返回 EAGAIN。
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (Connection::m_user_count >= MAX_FD)
        {
            Utils::Instance().show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (Connection::m_user_count >= MAX_FD)
            {
                Utils::Instance().show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    char signals[1024];
    // 从管道批量读出信号字节，逐个分发。
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // Reactor: 主线程只分发事件，worker 负责 Read + Process。
    if (1 == m_actormodel)
    {
        if (!m_pool->append(users[sockfd], threadpool<Connection>::TASK_READ)) {
            LOG_WARN("threadpool queue full, close fd %d", sockfd);
            deal_timer(timer, sockfd);
        }
    }
    else
    {
        // Proactor: 主线程完成 Read，worker 仅做 Process。
        LOG_INFO("dealwithread proactor mode, fd: %d, connection type: %s", sockfd, users[sockfd]->Type());
        if (users[sockfd]->Read())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd]->addr().sin_addr));
            if (!m_pool->append(users[sockfd], threadpool<Connection>::TASK_PROCESS)) {
                LOG_WARN("threadpool queue full, close fd %d", sockfd);
                deal_timer(timer, sockfd);
                return;
            }
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // Reactor: 主线程只分发写事件，worker 负责实际 Write。
    if (1 == m_actormodel)
    {
        if (!m_pool->append(users[sockfd], threadpool<Connection>::TASK_WRITE)) {
            LOG_WARN("threadpool queue full, close fd %d", sockfd);
            deal_timer(timer, sockfd);
        }
    }
    else
    {
        // Proactor: 主线程完成 Write。
        if (users[sockfd]->Write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd]->addr().sin_addr));
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    LOG_INFO("Entering event loop");

    int ping_tick = 0;

    while (!stop_server)
    {
        // epoll_wait 阻塞等待事件；被信号中断 (EINTR) 时继续循环。
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("epoll failure, errno: %d", errno);
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            else if ((sockfd == m_worker_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithworker();
                if (!flag)
                    LOG_ERROR("%s", "dealwithworker failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                // 普通读事件。
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 普通写事件。
                dealwithwrite(sockfd);
            }
        }

        if (timeout)
        {
            // 每个周期驱动一次定时器链表，关闭超时连接。
            Utils::Instance().timer_handler();

            // 每 5 个 tick (25s) 向所有 WebSocket 连接发送协议层 PING
            // 浏览器自动回复 PONG，不受标签页节流影响
            if (++ping_tick >= 5) {
                ping_tick = 0;
                ChatRoom::instance()->pingAll();
            }

            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }

    LOG_INFO("Exiting event loop");
}
void WebServer::initMessageBus() {
    // 跨服务器通信依赖 Redis Cluster，不依赖 MySQL
    // m_enable_db 只影响用户/好友数据持久化方式，不影响消息总线

    std::string serverId;
    if (!server_announce_ip.empty()) {
        serverId = server_announce_ip + ":" + std::to_string(m_port);
    } else {
        // 自动获取本机 IP
        char buf[128] = {0};
        FILE* fp = popen("hostname -I | awk '{print $1}'", "r");
        if (fp) {
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\n")] = 0;
                serverId = std::string(buf) + ":" + std::to_string(m_port);
            }
            pclose(fp);
        }
    }
    if (serverId.empty()) {
        LOG_WARN("initMessageBus: could not determine server ID, cross-server chat disabled");
        return;
    }

    LOG_INFO("initMessageBus: server_id=%s", serverId.c_str());

    // 1) OnlineUserManager
    std::string redisErr;
    bool redisOk = OnlineUserManager::instance()->initRedis(
        m_redis_host, m_redis_port, m_redis_pass, &redisErr);
    if (!redisOk) {
        LOG_WARN("OnlineUserManager Redis init failed: %s", redisErr.c_str());
    }
    OnlineUserManager::instance()->setServerId(serverId);

    // 2) MessageDispatcher
    MessageDispatcher::instance()->setServerId(serverId);
    MessageDispatcher::instance()->setDeliverCallback(
        [](const std::string& user, const std::string& msg) -> bool {
            return ChatRoom::instance()->deliverLocal(user, msg);
        });
    MessageDispatcher::instance()->setBroadcastCallback(
        [](const std::string& msg) {
            ChatRoom::instance()->broadcastLocal(msg, nullptr);
        });

    // 3) ChatMessageBus
    if (!ChatMessageBus::instance()->init(
            m_redis_host, m_redis_port, m_redis_pass, serverId, &redisErr)) {
        LOG_WARN("ChatMessageBus init failed: %s — cross-server chat disabled", redisErr.c_str());
        return;
    }
    ChatMessageBus::instance()->setMessageCallback(
        [](const std::string& msgId, const std::string& data) {
            MessageDispatcher::instance()->onBusMessage(msgId, data);
        });
    LOG_INFO("initMessageBus: ready");
}
