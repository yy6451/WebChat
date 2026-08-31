/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/threadpool/threadpool.h
 * 类型: Header
 * 作用: 线程池模块：负责任务排队、线程调度与并发执行框架。
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
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../sql/sql_connection_pool.h"

// ============================================================
// 线程池模板类
//
// 支持两种 IO 多路复用模型：Reactor 和 Proactor。
// 模板参数 T 代表任务类型，当前项目中为 Connection 或其派生类
//                                 （HttpConnection / WebSocketConnection）。
//
// Reactor 模式 (actor_model == 1):
//   主线程仅负责分发事件（TASK_READ / TASK_WRITE / TASK_PROCESS），
//   工作线程负责执行实际的 Read / Write / Process 操作。
//   适用场景：业务逻辑较重，主线程不想阻塞在 IO 处理上。
//
//   ┌──────────┐  epoll_wait   ┌──────────────┐
//   │ 主线程     │─────────────→│ 检测就绪事件   │
//   │ eventLoop │              │ 分发 TASK_*   │
//   └──────────┘              └──────┬───────┘
//                                    │ append(task, TASK_READ)
//                                    ▼
//                          ┌──────────────────┐
//                          │ 工作线程 (N 条)    │
//                          │ Read → Process   │
//                          │ Write             │
//                          │ → m_done_cb 通知  │
//                          └──────────────────┘
//
// Proactor 模式 (actor_model != 1):
//   主线程自行完成 Read / Write（IO 操作），仅将业务处理（Process）
//   交给工作线程。IO 操作在单线程上下文中完成，避免多线程竞争 fd。
//   适用场景：IO 密集、业务逻辑简单或需要严格串行化 IO。
//
//   ┌──────────┐  epoll_wait   ┌──────────────┐
//   │ 主线程     │─────────────→│ Read / Write  │ ← 主线程自己做 IO
//   │ eventLoop │              │ (同步非阻塞)   │
//   └──────────┘              └──────┬───────┘
//                                    │ append(task, TASK_PROCESS)
//                                    ▼
//                          ┌──────────────────┐
//                          │ 工作线程 (N 条)    │
//                          │ Process (纯业务)  │
//                          │ → m_done_cb 通知  │
//                          └──────────────────┘
//
// 线程安全设计：
//   - m_queuelocker (mutex): 保护 m_workqueue 的并发访问
//   - m_queuestat (semaphore): 实现生产者-消费者模型
//     · 生产者 (主线程 append): post() 唤醒一个工作线程
//     · 消费者 (工作线程 run): wait() 阻塞等待任务
//   - 任务完成后通过 m_done_cb 回调通知主线程（仅 Reactor 模式）
//   - 连接对象 (Connection) 的生命周期由主线程管理，
//     工作线程不 delete 连接对象
//
// 优雅关闭说明：
//   当前版本线程通过 pthread_detach 分离，析构时不会 join。
//   未来可改进为：增设 m_stop 原子标志 + m_queuestat.post() 广播
//   唤醒所有线程，然后在析构中 join。
// ============================================================

template <typename T>
class threadpool
{
public:
    // ---- 任务类型 ----
    // Reactor 模式下三种任务都会用到
    // Proactor 模式下仅 TASK_PROCESS 被使用
    enum TaskType {
        TASK_READ    = 0,   // 从 socket 读取数据到读缓冲区
        TASK_WRITE   = 1,   // 将写缓冲区数据发送到 socket
        TASK_PROCESS = 2,   // 纯业务逻辑处理（如 HTTP 解析、WebSocket 帧处理）
    };

    // 任务完成回调函数指针类型
    // @param context: 回调上下文（通常为 WebServer*）
    // @param sockfd:  任务对应的 socket fd
    // @param ok:      任务执行结果，false 表示需要关闭连接
    typedef void (*TaskDoneCallback)(void *context, int sockfd, bool ok);

    /*
     * 构造函数：创建线程池并启动工作线程。
     *
     * @param actor_model:   并发模型选择
     *                       1 = Reactor（主线程只分发，worker 做 IO+业务）
     *                       其他 = Proactor（主线程做 IO，worker 只做业务）
     * @param connPool:      数据库连接池指针，worker 线程内部可借还 MYSQL 连接
     * @param done_cb:       任务完成回调（Reactor 模式用，Proactor 模式传 nullptr）
     * @param done_ctx:      回调上下文（Reactor 模式通常传 WebServer*）
     * @param thread_number: 工作线程数量，默认 8
     * @param max_request:   任务队列最大容量，默认 10000。
     *                       超过此值 append() 返回 false，
     *                       调用方应丢弃任务或关闭连接（防内存撑爆）。
     */
    threadpool(int actor_model, connection_pool *connPool,
               TaskDoneCallback done_cb = nullptr, void *done_ctx = nullptr,
               int thread_number = 8, int max_request = 10000);
    ~threadpool();

    /*
     * 提交任务到线程池。
     *
     * 调用方（主线程 eventLoop）在检测到 fd 就绪事件后，
     * 通过此方法将连接对象和任务类型入队。
     *
     * @param request:  连接对象指针（Connection 或其派生类）
     * @param taskType: 任务类型（TASK_READ / TASK_WRITE / TASK_PROCESS）
     * @return: true 表示入队成功，false 表示队列已满（调用方应关闭连接）
     *
     * 线程安全：内部使用 m_queuelocker 加锁 + m_queuestat.post() 唤醒。
     * 复杂度 O(1)（list::push_back + sem::post）。
     */
    bool append(T *request, TaskType taskType);

private:
    /*
     * 工作线程入口函数。
     *
     * 必须是静态函数（pthread_create 要求），
     * 参数 arg 实际上是 threadpool* 类型的 this 指针。
     * 内部调用 run() 进入真正的任务循环。
     */
    static void *worker(void *arg);

    /*
     * 工作线程主循环（生产者-消费者模型中的消费者）。
     *
     * 循环逻辑：
     *   1) m_queuestat.wait()      阻塞等待任务信号
     *   2) m_queuelocker.lock()    加锁取队首任务
     *   3) m_workqueue.pop_front() 出队
     *   4) m_queuelocker.unlock()  解锁
     *   5) 根据 TaskType 执行 Read/Write/Process
     *   6) 调用 m_done_cb 通知主线程任务完成
     *
     * Reactor 模式下，第 5 步包括 IO 操作和业务处理；
     * Proactor 模式下，第 5 步只包括业务处理（Process）。
     *
     * 注意：该函数不返回值且无退出条件，
     *       线程生命周期与进程一致（通过 pthread_detach 分离）。
     */
    void run();

private:
    // ---- 任务队列元素 ----
    struct TaskItem {
        T *request;          // 连接对象指针（多态基类）
        TaskType taskType;   // 任务类型
    };

    // ---- 配置参数 ----
    int m_thread_number;         // 工作线程数量
    int m_max_requests;          // 任务队列最大长度（注：signed，与 size_t 比较可能 warning）
    pthread_t *m_threads;        // 动态分配的 pthread_t 数组，大小为 m_thread_number

    // ---- 任务队列与同步原语 ----
    std::list<TaskItem> m_workqueue;  // 任务队列（FIFO，list 保证 push_back/pop_front O(1)）
    locker m_queuelocker;             // 互斥锁，保护 m_workqueue 的并发读写
    sem m_queuestat;                  // 信号量，实现生产者-消费者间的通知：
                                      //   sem::wait()  消费端阻塞等待
                                      //   sem::post()  生产端发出通知

    // ---- 外部依赖 ----
    connection_pool *m_connPool;      // 数据库连接池，worker 线程处理 HTTP 请求时使用
    int m_actor_model;                // 并发模型标记：1 = Reactor, 其他 = Proactor

    // ---- 任务完成通知 ----
    TaskDoneCallback m_done_cb;       // 每个任务执行完毕后调用
    void *m_done_ctx;                 // 回调上下文指针（如 WebServer*）
};

// ============================================================
// 构造函数实现
//
// 初始化顺序：
//   1) 拷贝所有配置参数到成员变量
//   2) 参数校验（thread_number <= 0 或 max_requests <= 0 抛出异常）
//   3) new pthread_t[m_thread_number] 分配线程 ID 数组
//   4) 循环 pthread_create 创建工作线程
//   5) pthread_detach 分离线程（线程结束后由 OS 自动回收资源）
//
// 为什么用 pthread_detach 而不是 pthread_join？
//   - 线程池生命周期 ≈ 进程生命周期，不需要在析构中等待线程退出
//   - 简化析构逻辑（~threadpool 只需 delete[] m_threads）
//   - 缺点：无法实现优雅关闭（线程可能在任务中途被强制终止）
//
// 为什么用 new 而不是 vector？
//   - 历史遗留（原项目风格），改为 vector<pthread_t> 可减少手动内存管理
// ============================================================
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool,
                          TaskDoneCallback done_cb, void *done_ctx,
                          int thread_number, int max_requests)
    // ---- 成员初始化列表 ----
    : m_actor_model(actor_model),   // 并发模型（1=Reactor, 其他=Proactor）
      m_thread_number(thread_number), // 工作线程数
      m_max_requests(max_requests),   // 队列容量上限
      m_threads(NULL),                // 延迟 new 分配（先置空，防异常时 delete 野指针）
      m_connPool(connPool),           // 数据库连接池
      m_done_cb(done_cb),             // 任务完成回调
      m_done_ctx(done_ctx)            // 回调上下文
{
    // ---- 参数校验 ----
    // 线程数和队列大小必须为正数，否则无法正常工作
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    // ---- 分配线程 ID 数组 ----
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    // ---- 创建工作线程并分离 ----
    // 每个线程的入口函数是 worker(this)，内部调用 run() 进入主循环
    for (int i = 0; i < thread_number; ++i)
    {
        // pthread_create: 创建线程，属性默认(NULL)，入口 worker，参数 this
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;      // 创建失败时清理已分配资源
            throw std::exception();
        }
        // pthread_detach: 分离线程，线程结束后由 OS 回收，不需 join
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;      // 分离失败时清理
            throw std::exception();
        }
    }
}

// ============================================================
// 析构函数
//
// 只释放 pthread_t 数组。
// 工作线程已通过 pthread_detach 分离，不会尝试 join。
//
// 注意：如果队列中还有未处理的任务，这些任务会丢失。
//       生产环境建议先设置停止标志 + 广播信号量唤醒所有线程，
//       再 join 等待线程自然退出。
// ============================================================
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// ============================================================
// append: 提交任务到线程池（生产者）
//
// 流程：
//   1) m_queuelocker.lock()    加锁保护队列
//   2) 检查队列长度是否到达上限 m_max_requests
//      - 到达上限 → unlock + 返回 false（调用方应关闭连接）
//      - 未满     → 构造 TaskItem 并 push_back
//   3) m_queuelocker.unlock()  解锁
//   4) m_queuestat.post()      信号量 +1，唤醒一个等待中的工作线程
//
// 返回值：
//   true  - 入队成功
//   false - 队列已满，任务被拒绝
//
// 线程安全：
//   - push_back 在锁内执行，与 run() 中的 pop_front 互斥
//   - post() 在锁外执行，减少临界区长度
//
// 为什么用信号量而不是条件变量？
//   - sem::post() 是异步的，不要求在锁内执行
//   - 比条件变量 (pthread_cond_signal) 更简洁
//   - 缺点：信号量计数可能累积（如果 worker 假唤醒），
//           但通过 run() 中的 empty() 二次检查兜底
// ============================================================
template <typename T>
bool threadpool<T>::append(T *request, TaskType taskType)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= static_cast<size_t>(m_max_requests))
    {
        m_queuelocker.unlock();
        return false;               // 队列满，拒绝任务
    }
    TaskItem item;
    item.request = request;
    item.taskType = taskType;
    m_workqueue.push_back(item);    // 尾插，O(1)
    m_queuelocker.unlock();

    m_queuestat.post();             // 信号量 +1，唤醒一个消费线程
    return true;
}

// ============================================================
// worker: pthread 入口函数（静态，C 风格）
//
// 为什么需要静态函数：
//   pthread_create 要求入口签名为 void*(*)(void*)，
//   非静态成员函数有隐含的 this 指针，签名不兼容。
//
// arg 参数：
//   指向 threadpool 实例的 this 指针。
//   这里通过 C 风格类型转换 (threadpool*)arg 恢复类型，
//   然后调用 run() 进入真正的任务处理循环。
// ============================================================
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = static_cast<threadpool *>(arg);
    pool->run();
    return pool;
}

// ============================================================
// run: 工作线程主循环（消费者，永不退出）
//
// 这是线程池的核心消费逻辑，每个工作线程在此循环中不断：
//   1) 等待任务信号
//   2) 从队列取任务
//   3) 执行任务
//   4) 通知主线程
//
// 详细流程：
//
//   while (true) {
//     ┌─ m_queuestat.wait()      阻塞等待，队列空时线程睡眠
//     │                          当 append() 调用 post() 时被唤醒
//     │                          唤醒后信号量自动 -1
//     │
//     ├─ m_queuelocker.lock()    加锁取任务
//     │
//     ├─ if (m_workqueue.empty())
//     │     continue             假唤醒保护：队列空则重新等待
//     │                          （理论上不会发生，但加一层防御）
//     │
//     ├─ item = m_workqueue.front()
//     │    m_workqueue.pop_front()
//     │                          出队，O(1)
//     │
//     ├─ m_queuelocker.unlock()  尽快解锁，不阻塞其他线程
//     │
//     ├─ 根据 TaskType 执行：
//     │
//     │   TASK_READ (Reactor):
//     │     ok = request->Read()     ← 从 socket 读数据
//     │     if (ok) request->Process() ← 读成功后处理业务
//     │
//     │   TASK_WRITE (Reactor):
//     │     ok = request->Write()    ← 向 socket 写数据
//     │
//     │   TASK_PROCESS (Proactor / Reactor):
//     │     request->Process()       ← 纯业务处理
//     │
//     ├─ if (m_done_cb)
//     │     m_done_cb(m_done_ctx, sockfd, ok)
//     │                          通知主线程任务完成
//     │                          (Reactor 模式下通过 pipe 回传结果)
//     │
//     └─ 循环回到 wait()，等待下一个任务
//   }
//
// Reactor vs Proactor 在这个函数中的差异：
//
//   ┌──────────┬─────────────────────────────────────┐
//   │ 任务类型  │     Reactor        │   Proactor     │
//   ├──────────┼─────────────────────┼────────────────┤
//   │TASK_READ │ worker 做 Read +    │ 不使用 (主线程 │
//   │          │ Process             │ 自己完成 Read) │
//   ├──────────┼─────────────────────┼────────────────┤
//   │TASK_WRITE│ worker 做 Write     │ 不使用 (主线程 │
//   │          │                     │ 自己完成 Write)│
//   ├──────────┼─────────────────────┼────────────────┤
//   │TASK_     │ worker 做 Process   │ worker 做      │
//   │PROCESS   │ (基本不使用)        │ Process        │
//   └──────────┴─────────────────────┴────────────────┘
//
// fd == -1 检查：
//   request->Process() 内部可能调用 Close() 将 sockfd_ 设为 -1，
//   此时表示连接已关闭，ok 置为 false 通知主线程回收资源。
// ============================================================
template <typename T>
void threadpool<T>::run()
{
    while (true)                      // 永不退出的消费循环
    {
        // ---- 1) 等待任务信号 ----
        m_queuestat.wait();           // 信号量阻塞等待
                                      // append() 调用 post() 时唤醒

        // ---- 2) 加锁从队列取任务 ----
        m_queuelocker.lock();
        if (m_workqueue.empty())      // 假唤醒保护
        {
            m_queuelocker.unlock();
            continue;                 // 重新等待
        }

        TaskItem item = m_workqueue.front();   // 取队首
        m_workqueue.pop_front();               // 出队，O(1)
        m_queuelocker.unlock();                // 尽快解锁

        T *request = item.request;
        if (!request)                 // 防御：空指针跳过
            continue;

        const int sockfd = request->fd();  // 记录 fd 用于回调通知
        bool ok = true;                    // 任务执行结果标记

        // ---- 3) 根据任务类型执行 ----
        if (item.taskType == TASK_READ) {
            // Reactor 读任务：先 Read 再 Process
            ok = request->Read();     // 从 socket 读数据到 readBuffer_
            if (ok) {
                request->Process();   // 解析 HTTP 请求 / WebSocket 帧
                if (request->fd() == -1) {
                    ok = false;       // Process 中关闭了连接，标记失败
                }
            }
        } else if (item.taskType == TASK_WRITE) {
            // Reactor 写任务
            ok = request->Write();    // 将 writeBuffer_ 数据发送到 socket
        } else {
            // Proactor 业务任务 or Reactor 纯业务任务
            request->Process();       // 协议解析 / 业务逻辑
            if (request->fd() == -1) {
                ok = false;           // Process 中关闭了连接
            }
        }

        // ---- 4) 通知主线程任务完成 ----
        if (m_done_cb) {
            m_done_cb(m_done_ctx, sockfd, ok);
            // Reactor 模式下，回调内部将结果写入 pipe，
            // 主线程 eventLoop 收到 pipe 可读事件后调用 dealwithworker()
        }
    }
}

#endif
