/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/config/config.cpp
 * 类型: Source
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
#include "config.h"

Config::Config(){
    //端口号,默认9007
    PORT = 9007;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 0;

    //listenfd触发模式，默认LT
    LISTENTrigmode = 0;

    //connfd触发模式，默认LT
    CONNTrigmode = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认2（适配2核CPU）
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;

    //并发模型,默认是proactor
    actor_model = 0;

    // Redis 连接参数默认值
    redis_host = "127.0.0.1";
    redis_port = 7000;
    redis_db = 0;
    redis_pass = "";
    // MySQL 连接参数默认值
    mysql_host = "localhost";

    // 本服务器对外 IP（跨服务器消息去重用）
    server_announce_ip = "";

    // 默认启用数据库
    enable_db = 1;

    // 默认心跳超时 90s（心跳 30s × 3）
    heartbeat_timeout = 90;
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:d:H:R:P:N:A:M:I:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        case 'R':
        {
            redis_host = optarg;
            break;
        }
        case 'P':
        {
            redis_port = atoi(optarg);
            break;
        }
        case 'N':
        {
            redis_db = atoi(optarg);
            break;
        }
        case 'A':
        {
            redis_pass = optarg;
            break;
        }
        case 'd':
        {
            enable_db = atoi(optarg);
            break;
        }
        case 'H':
        {
            heartbeat_timeout = atoi(optarg);
            break;
        }
        case 'M':
        {
            mysql_host = optarg;
            break;
        }
        case 'I':
        {
            server_announce_ip = optarg;
            break;
        }
        default:
            break;
        }
    }
}