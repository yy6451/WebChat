/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/log/log.h
 * 类型: Header
 * 作用: 日志模块：负责同步/异步日志写入、日志级别控制与落盘策略。
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
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "../block_queue.h"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
        return nullptr;
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

    int get_close_log() const { return m_close_log; }

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
        return nullptr;
    }

private:
    char dir_name[512]; //路径名
    char log_name[512]; //日志文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};

#define LOG_DEBUG(format, ...) if(0 == Log::get_instance()->get_close_log()) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == Log::get_instance()->get_close_log()) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == Log::get_instance()->get_close_log()) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == Log::get_instance()->get_close_log()) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
