/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/lock/locker.h
 * 类型: Header
 * 作用: 项目源码文件：承载服务端功能实现与基础设施能力。
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
#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// ============================================================
// sem: POSIX 信号量 (sem_t) 封装
//
// 信号量用于线程间的同步通知。
// 与互斥锁不同，信号量的 post() 不需要在同一个线程中调用 wait()，
// 因此常用于生产者-消费者模型中的跨线程事件通知。
//
// 内部变量:
//   m_sem: sem_t 实例，kernel 级信号量（可跨进程共享，但本项目仅在线程间使用）
//
// 使用示例（生产者-消费者）:
//   sem s(0);            // 初始值 0
//   生产者: s.post();    // 信号量 +1，唤醒消费者
//   消费者: s.wait();    // 信号量 -1，=0 时阻塞
//
// 为什么不用 std::counting_semaphore (C++20)?
//   - 项目使用 C++17，不支持
//   - pthread 信号量在 Linux 上是成熟的 POSIX 实现
// ============================================================
class sem
{
public:
    // ---- 默认构造函数: 信号量初始值=0 ----
    // sem_init(&m_sem, 0, 0):
    //   第二个参数 0: pshared=0，仅限本进程内线程共享（非进程间）
    //   第三个参数 0: 信号量初值=0
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }

    // ---- 带参构造函数: 信号量初始值=num ----
    // 用于线程池初始化：让线程池预先有一定数量的"可用资源"计数
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }

    // ---- 析构: 销毁信号量 ----
    // sem_destroy 释放内核资源
    ~sem()
    {
        sem_destroy(&m_sem);
    }

    // ---- wait: 信号量 P 操作 (减 1) ----
    // sem_wait:
    //   若信号量值 > 0 → 减 1 并立即返回 true
    //   若信号量值 = 0 → 线程阻塞，直到其他线程调用 post()
    // 返回 true 表示成功获取信号量
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // ---- post: 信号量 V 操作 (加 1) ----
    // sem_post:
    //   信号量值 +1
    //   如果有线程在 wait() 中阻塞，唤醒其中一个
    // 返回 true 表示成功
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;  // POSIX 信号量
};


// ============================================================
// locker: POSIX 互斥锁 (pthread_mutex_t) 封装
//
// 互斥锁用于保护临界区，保证同一时刻只有一个线程访问共享数据。
//
// 与 std::mutex 的对比:
//   - 本项目使用 pthread_mutex_t，配合 cond 类使用
//   - std::mutex 不能直接传给 pthread_cond_wait（需要 pthread_mutex_t*）
//   - 如果项目中不再使用 cond，可以迁移到 std::mutex
//
// 注意: 本类不提供 RAII 风格的 lock_guard，
//       需要手动 lock()/unlock() 配对调用。
//       未来可增加 scoped_lock 内部类实现自动解锁。
//
// 使用示例:
//   locker l;
//   l.lock();
//   // ... 临界区代码 ...
//   l.unlock();
//
// 线程安全语义:
//   - lock():   若锁已被其他线程持有，则阻塞等待
//   - unlock(): 释放锁，唤醒一个等待中的线程
//   - get():    返回底层 pthread_mutex_t* 指针
//               (供 cond::wait() 使用，pthread_cond_wait 需要 mutex 参数)
// ============================================================
class locker
{
public:
    // ---- 构造函数: 初始化互斥锁 ----
    // pthread_mutex_init(&m_mutex, NULL):
    //   第二个参数 NULL: 使用默认属性（普通锁，非递归）
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }

    // ---- 析构: 销毁互斥锁 ----
    // pthread_mutex_destroy 释放资源
    // 确保调用时锁未被持有，否则行为未定义
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    // ---- lock: 获取互斥锁 ----
    // 如果锁已被其他线程持有，当前线程阻塞等待
    // 返回 true 表示加锁成功
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // ---- unlock: 释放互斥锁 ----
    // 必须在持有锁的线程中调用
    // 释放后唤醒一个等待该锁的线程
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // ---- get: 获取底层 pthread_mutex_t 指针 ----
    // 用于配合 pthread_cond_wait 使用：
    //   cond::wait(mutex.get())
    //   条件变量在 wait 时会原子地释放 mutex 并进入等待
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;  // POSIX 互斥锁
};


// ============================================================
// cond: POSIX 条件变量 (pthread_cond_t) 封装
//
// 条件变量用于线程之间"等待某个条件成立"的通知。
// 比信号量更灵活：可以配合 while 循环实现「条件判断 → 等待 → 被唤醒 → 重新判断」的模式。
//
// 与信号量 (sem) 的对比:
//   ┌──────────┬─────────────────────┬────────────────────────┐
//   │          │ sem (信号量)         │ cond (条件变量)         │
//   ├──────────┼─────────────────────┼────────────────────────┤
//   │通知方式   │ post() 计数+1       │ signal() 唤醒一个      │
//   │          │                     │ broadcast() 唤醒全部   │
//   ├──────────┼─────────────────────┼────────────────────────┤
//   │等待方式   │ wait() 计数-1       │ wait(mutex) 必须持有锁  │
//   │          │                     │ 内部原子释放锁+等待     │
//   ├──────────┼─────────────────────┼────────────────────────┤
//   │丢失唤醒   │ 不丢失(post 计数累积)│ 可能丢失(signal 无人等) │
//   ├──────────┼─────────────────────┼────────────────────────┤
//   │适用场景   │ 资源计数/生产者消费者 │ 条件等待(spurious wakeup│
//   │          │                     │ 需 while 保护)         │
//   └──────────┴─────────────────────┴────────────────────────┘
//
// 当前项目中的使用:
//   目前主要使用 sem（线程池任务通知），cond 类保留供未来扩展
//   （如阻塞队列 BlockQueue 中的条件等待）。
//
// wait(mutex) 的标准用法:
//   mutex.lock();
//   while (!condition_met) {         // ← while 防虚假唤醒
//       cond.wait(mutex.get());      // 原子地: unlock(mutex) → 等待 → lock(mutex)
//   }
//   // ... 条件满足后的处理 ...
//   mutex.unlock();
//
// timewait(mutex, t) 用法:
//   与 wait 相同，但增加了超时限制。
//   超时返回 false，用于「等待最多 N 秒」的场景（如心跳超时检测）。
// ============================================================
class cond
{
public:
    // ---- 构造函数: 初始化条件变量 ----
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }

    // ---- 析构: 销毁条件变量 ----
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    // ---- wait: 等待条件成立 ----
    // @param m_mutex: 已加锁的互斥锁指针
    //
    // pthread_cond_wait 内部执行:
    //   1) 原子地释放 mutex（允许其他线程修改条件）
    //   2) 线程进入等待队列睡眠
    //   3) 被 signal/broadcast 唤醒后，原子地重新获取 mutex
    //   4) 返回
    //
    // 返回值:
    //   true  - 被正常唤醒
    //   false - 发生错误
    //
    // 注意: 调用此方法前 mutex 必须已 lock。
    //       返回时 mutex 保持 lock 状态。
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    // ---- timewait: 限时等待条件成立 ----
    // @param m_mutex: 已加锁的互斥锁指针
    // @param t:      绝对超时时间 (struct timespec)
    //
    // 与 wait() 的区别:
    //   - 超时返回 false（ETIMEDOUT），等待返回 true
    //   - 适合「最多等 N 秒」的场景
    //
    // struct timespec 设置示例:
    //   struct timespec ts;
    //   clock_gettime(CLOCK_REALTIME, &ts);
    //   ts.tv_sec += 5;  // 超时 5 秒
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }

    // ---- signal: 唤醒一个等待线程 ----
    // 如果有多个线程在 wait()，只唤醒其中一个
    // 如果没有线程在等待，信号丢失（不累积）
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // ---- broadcast: 唤醒所有等待线程 ----
    // 所有在 wait() 中阻塞的线程被同时唤醒
    // 它们会竞争 mutex，只有一个能先获得锁
    // 适用于「条件对所有人同时成立」的场景
    // （如线程池关闭时通知所有 worker 退出）
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;  // POSIX 条件变量
};

#endif
