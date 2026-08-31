/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/timer/lst_timer.h
 * 类型: Header
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
#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include <vector>

#include "../log/log.h"

// 前向声明
class WebServer;
class Connection;
class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
    Connection *conn;      // 指向连接对象
    WebServer *server;     // 指向所属的 WebServer 实例
};

// 每个时间轮槽位：一个侵入式双向链表头尾指针
struct TimerSlot
{
    util_timer *head;
    util_timer *tail;
    TimerSlot() : head(nullptr), tail(nullptr) {}
};

class util_timer
{
public:
    util_timer() : prev(nullptr), next(nullptr), rotation(0), slot_index(-1) {}

public:
    time_t expire;              // 绝对过期时间（调用方设置，时间轮不直接依赖）
    void (* cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;           // 槽内链表前驱
    util_timer *next;           // 槽内链表后继
    int rotation;               // 还需旋转的圈数（0 表示当前圈到期）
    int slot_index;             // 所属槽位编号，-1 表示未加入任何槽
};

// 经典时间轮定时器：所有操作 O(1)
// 每个槽是一个 FIFO 双向链表。
// 定时器通过 rotation 计数器处理多圈环绕：
//   - 插入时：rotation = ticks / num_slots
//   - tick 时：若 rotation > 0 则 decrement；若 rotation == 0 则到期执行回调
class TimerWheel
{
public:
    TimerWheel();
    ~TimerWheel();

    // 初始化时间轮
    // @param slot_interval: 每个槽代表的时间间隔（秒），即 tick 周期
    // @param max_timeout:   最大超时时间（秒），用于计算槽数
    void init(int slot_interval, int max_timeout);

    // 添加定时器 O(1)
    void add_timer(util_timer *timer);
    // 调整定时器（刷新过期时间，重新计算 rotation 与 slot）O(1)
    void adjust_timer(util_timer *timer);
    // 删除定时器 O(1)
    void del_timer(util_timer *timer);
    // 驱动时间轮前进一个槽，处理到期定时器
    void tick();

    size_t size() const { return timer_count_; }

private:
    // 从槽中摘除定时器（不 delete，不修改 rotation/slot_index）
    void _unlink(util_timer *timer);
    // 将定时器尾插到指定槽
    void _link(util_timer *timer, int slot);

    std::vector<TimerSlot> slots_;
    int num_slots_;
    int slot_interval_;     // 每槽秒数
    int current_slot_;      // 当前槽位（tick 先扫描，再前进）
    size_t timer_count_;    // 定时器总数
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 单例访问器：鼓励调用方使用统一实例以避免多个工具对象带来的状态分散。
    static Utils &Instance();

    void init(int timeslot, int max_timeout = 60);
    int setnonblocking(int fd);
    void modfd(int epollfd, int fd, int ev, int TRIGMode);
    void removefd(int epollfd, int fd);
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    static void sig_handler(int sig);
    void addsig(int sig, void(handler)(int), bool restart = true);
    void timer_handler();
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    TimerWheel m_timer_wheel;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

// 兼容层已移除：请改用 `Utils::Instance().addfd(...)` / `modfd(...)` / `removefd(...)` / `setnonblocking(...)`。

#endif