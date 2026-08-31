/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/core/connection/connection_factory.cpp
 * 类型: Source
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
#include "connection_factory.h"
#include "http_connection.h"

Connection* ConnectionFactory::Create(int sockfd, const sockaddr_in& addr,
                                      int trigmode, int close_log,
                                      const char* doc_root,
                                      WebServer* server) {
    // 目前仅支持 HTTP 连接，直接构造 HttpConnection 对象
    return new HttpConnection(sockfd, addr, trigmode, close_log,
                              doc_root, server);
}