/*
 * AutoComment: Detailed File Overview
 * 文件: backend/src/utils/sql/sql_connection_pool.h
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
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式
	static connection_pool *GetInstance();

	bool init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

	bool isInitialized() const { return m_MaxConn > 0; }

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; //连接池
	sem reserve;

public:
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};

class connectionRAII{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
