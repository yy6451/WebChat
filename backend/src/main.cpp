/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/main.cpp
 * 类型: Source
 * 作用: 程序入口文件：负责读取配置、初始化 WebServer 并启动主事件循环。
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
#include "utils/auth/auth_manager.h"
#include <unistd.h>

#include <string>

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "yy1";
    string passwd = "666888";
    string databasename = "mydb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    {
        std::string err;
        if (!AuthManager::instance()->initRedis(config.redis_host, config.redis_port,
                                                config.redis_pass, config.redis_db, &err)) {
            fprintf(stderr, "Auth Redis init failed: %s\n", err.c_str());
            return 1;
        }
    }

    // 统一切换到项目根目录，避免从不同 cwd 启动导致静态目录和日志目录错乱
    std::string projectRoot = ".";
    if (access("frontend/dist", F_OK) == 0) {
        projectRoot = ".";
    } else if (access("../frontend/dist", F_OK) == 0) {
        projectRoot = "..";
    } else if (access("/home/yy1/TinyWebServer/frontend/dist", F_OK) == 0) {
        projectRoot = "/home/yy1/TinyWebServer";
    }
    if (chdir(projectRoot.c_str()) != 0) {
        perror("chdir project root failed");
    }
    std::string docRoot = "frontend/dist";

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model, docRoot,
                config.enable_db, config.heartbeat_timeout, config.mysql_host,
                config.server_announce_ip, config.redis_host, config.redis_port,
                config.redis_pass);
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}