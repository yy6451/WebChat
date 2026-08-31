/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/timer/lst_timer.cpp
 * 类型: Source
 * 作用: 定时器模块：负责连接超时管理与定时回调触发。
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
#include "lst_timer.h"
#include "core/webserver.h"               // 需要 WebServer 定义以访问 users 数组
#include "core/connection/connection.h"    // Connection 基类

// ============================================================
// TimerWheel 实现 —— 经典时间轮，所有操作 O(1)
//
// 核心思想：
//   将时间划分为 N 个槽，每个槽间隔 slot_interval_ 秒。
//   定时器按 "还需要多少 tick 才到期" 决定插入哪个槽：
//     ticks   = timeout / slot_interval_
//     rotation = ticks / num_slots_          (还需转多少圈)
//     slot     = (current + ticks % num_slots_) % num_slots_
//   tick() 时只扫描 current_slot_：
//     若 rotation > 0 → rotation-- (未到期，等下一圈)
//     若 rotation == 0 → 执行回调 + delete
//   O(1) 的关键：add/del/adjust 只做链表尾插/摘除 + 简单算术。
// ============================================================

TimerWheel::TimerWheel()
    : num_slots_(0), slot_interval_(0), current_slot_(0),
      timer_count_(0)
{
}

TimerWheel::~TimerWheel()
{
    // 清理所有槽中残留的定时器（正常退出时不应有残留）
    for (int i = 0; i < num_slots_; ++i)
    {
        util_timer *tmp = slots_[i].head;
        while (tmp)
        {
            util_timer *next = tmp->next;
            delete tmp;
            tmp = next;
        }
    }
}

void TimerWheel::init(int slot_interval, int max_timeout)
{
    slot_interval_ = slot_interval > 0 ? slot_interval : 1;

    // 经典公式：槽数 = 最大超时 / 槽间隔 + 1（当前槽）
    // 例如 max_timeout=60, slot_interval=5 → 60/5 + 1 = 13 槽
    // rotation 机制保证即使 timeout 超过槽数也能正确处理
    num_slots_ = (max_timeout / slot_interval_) + 1;
    if (num_slots_ < 8) num_slots_ = 8;   // 最小槽数保护

    slots_.resize(num_slots_);
    current_slot_ = 0;
    timer_count_ = 0;
}

// ---------- 链表操作（不变） ----------

void TimerWheel::_link(util_timer *timer, int slot)
{
    TimerSlot &ts = slots_[slot];
    timer->prev = ts.tail;
    timer->next = nullptr;
    timer->slot_index = slot;

    if (!ts.head)
    {
        ts.head = ts.tail = timer;
    }
    else
    {
        ts.tail->next = timer;
        ts.tail = timer;
    }
}

void TimerWheel::_unlink(util_timer *timer)
{
    int slot = timer->slot_index;
    if (slot < 0 || slot >= num_slots_)
        return;

    TimerSlot &ts = slots_[slot];

    if (timer == ts.head)
    {
        ts.head = timer->next;
        if (ts.head)
            ts.head->prev = nullptr;
        else
            ts.tail = nullptr;
    }
    else if (timer == ts.tail)
    {
        ts.tail = timer->prev;
        if (ts.tail)
            ts.tail->next = nullptr;
    }
    else
    {
        if (timer->prev) timer->prev->next = timer->next;
        if (timer->next) timer->next->prev = timer->prev;
    }

    timer->prev = nullptr;
    timer->next = nullptr;
    timer->slot_index = -1;
}

// ---------- 核心接口 ----------

void TimerWheel::add_timer(util_timer *timer)
{
    if (!timer || num_slots_ == 0)
        return;

    // 从 timer->expire 反推剩余超时秒数（保持与调用方的兼容）
    time_t now = time(nullptr);
    int timeout = static_cast<int>(timer->expire - now);
    if (timeout < 0) timeout = 0;

    // 经典时间轮算法：
    //   ticks   = 需要经过多少个 tick 才到期
    //   rotation = ticks / num_slots  → 还需要转多少圈
    //   slot     = current + (ticks % num_slots) 取模
    // 这里采用向上取整：任何小于一个槽间隔的超时，都应放到“下一次 tick”而不是当前槽，
    // 否则 timer 可能在当前槽刚被扫描完后才加入，进而被延迟整整一轮。
    int ticks = timeout <= 0 ? 1 : (timeout + slot_interval_ - 1) / slot_interval_;
    timer->rotation = ticks / num_slots_;
    int slot = (current_slot_ + (ticks % num_slots_)) % num_slots_;

    _link(timer, slot);
    ++timer_count_;
}

void TimerWheel::adjust_timer(util_timer *timer)
{
    if (!timer || num_slots_ == 0 || timer->slot_index < 0)
        return;

    // 从旧槽位摘除 → 重新计算 rotation 与 slot → 插入新槽位
    _unlink(timer);
    --timer_count_;
    add_timer(timer);
}

void TimerWheel::del_timer(util_timer *timer)
{
    if (!timer || num_slots_ == 0)
        return;

    // 已经不在轮上的 timer 直接忽略，避免重复删除/重复回收。
    if (timer->slot_index < 0)
        return;

    _unlink(timer);
    --timer_count_;
    delete timer;
}

void TimerWheel::tick()
{
    if (num_slots_ == 0)
        return;

    // 1) 扫描当前槽的所有定时器：
    //    - rotation > 0  → 还未到，rotation-- 留在这个槽等下一圈
    //    - rotation == 0 → 到期，执行回调并删除
    TimerSlot &ts = slots_[current_slot_];
    util_timer *tmp = ts.head;
    while (tmp)
    {
        util_timer *next = tmp->next;   // 提前保存，防 delete 后悬空
        if (tmp->rotation > 0)
        {
            tmp->rotation--;
        }
        else
        {
            _unlink(tmp);
            --timer_count_;
            tmp->cb_func(tmp->user_data);
            delete tmp;
        }
        tmp = next;
    }

    // 2) 扫描完成后再前进到下一个槽
    current_slot_ = (current_slot_ + 1) % num_slots_;
}

void Utils::init(int timeslot, int max_timeout)
{
    m_TIMESLOT = timeslot;
    m_timer_wheel.init(timeslot, max_timeout);
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    if (old_option == -1) {
        LOG_ERROR("fcntl F_GETFL failed: %d", errno);
        return -1;
    }
    int new_option = old_option | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, new_option) == -1) {
        LOG_ERROR("fcntl F_SETFL failed: %d", errno);
        return -1;
    }
    // 设置 FD_CLOEXEC，避免子进程继承 fd
    int fd_flags = fcntl(fd, F_GETFD);
    if (fd_flags != -1) {
        fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        LOG_ERROR("epoll_ctl ADD failed: %d", errno);
        return;
    }
    setnonblocking(fd);
}

void Utils::modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) == -1) {
        LOG_ERROR("epoll_ctl MOD failed: %d", errno);
        return;
    }
}

void Utils::removefd(int epollfd, int fd)
{
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) == -1) {
        LOG_ERROR("epoll_ctl DEL failed: %d", errno);
        // 如果删除失败也尝试关闭 fd
    }
    close(fd);
}

void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    if (sigaction(sig, &sa, nullptr) == -1) {
        perror("sigaction failed");
        exit(1);
    }
}

void Utils::timer_handler()
{
    m_timer_wheel.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = nullptr;
int Utils::u_epollfd = -1;

// 单例实现
Utils &Utils::Instance()
{
    static Utils instance;
    return instance;
}

// 定时器回调函数：关闭连接并清理资源
void cb_func(client_data *user_data)
{
    if (!user_data)
        return;

    int sockfd = user_data->sockfd;
    Connection *conn = user_data->conn;
    WebServer *server = user_data->server;

    if (!conn || !server)
        return;

    // 1. 关闭连接（会从 epoll 移除、关闭 socket、减少 m_user_count）
    conn->Close();

    // 2. 将 WebServer 中对应的 users 数组位置置空（不删除对象，由deal_timer处理）
    if (sockfd >= 0 && sockfd < MAX_FD) {
        server->users[sockfd] = nullptr;
        user_data->timer = nullptr;
        user_data->conn = nullptr;
    }

    // 注意：定时器对象本身会在 TimerWheel::tick 或 del_timer 中被 delete，
    // 这里不再重复处理。
}