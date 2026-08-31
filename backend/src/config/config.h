/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/config/config.h
 * 类型: Header
 * 作用: 配置模块：负责命令行参数解析与运行参数存储。
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
#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

    //日志写入方式
    int LOGWrite;

    //触发组合模式
    int TRIGMode;

    //listenfd触发模式
    int LISTENTrigmode;

    //connfd触发模式
    int CONNTrigmode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型选择
    int actor_model;

    // Redis 连接参数
    std::string redis_host;
    int redis_port;
    int redis_db;
    std::string redis_pass;

    // MySQL 连接参数
    std::string mysql_host;

    // 本服务器对外 IP（用于跨服务器消息去重标识）
    std::string server_announce_ip;

    // 是否启用数据库鉴权: 0=禁用(默认), 1=启用
    int enable_db;

    // 心跳超时时间(秒)
    int heartbeat_timeout;
};

#endif