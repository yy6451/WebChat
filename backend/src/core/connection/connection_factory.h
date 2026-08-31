/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/connection_factory.h
 * 类型: Header
 * 作用: 连接层模块：封装连接生命周期与协议分发（HTTP/WebSocket）。
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
#ifndef CONNECTION_FACTORY_H
#define CONNECTION_FACTORY_H

#include <netinet/in.h>
#include <string>
#include "connection.h"
#include "../webserver.h"

class ConnectionFactory {
public:
    /**
     * @brief 创建连接的静态工厂方法，当前仅生成 HttpConnection 对象
     * @param sockfd 已接受的套接字描述符
     * @param addr 客户端地址信息
     * @param trigmode 连接使用的触发模式（LT/ET）
     * @param close_log 是否关闭日志（来自配置）
     * @param doc_root 网站根目录路径
     * @param server 指向 WebServer 的指针
     * @return Connection* 指向新创建的 Connection 对象的指针（调用者负责管理）
     */
    static Connection* Create(int sockfd, const sockaddr_in& addr,
                              int trigmode, int close_log,
                              const char* doc_root,
                              WebServer* server);
};

#endif // CONNECTION_FACTORY_H