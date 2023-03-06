/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

//使用单例模式和队列创建数据库连接池，实现对数据库连接资源的复用。
//数据库连接池的定义
class SqlConnPool {
public:
    //局部静态变量单例模式
    static SqlConnPool *Instance();

    MYSQL *GetConn(); //获取数据库连接
    void FreeConn(MYSQL * conn); //释放连接
    int GetFreeConnCount(); //获取连接

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize); //初始化连接池
    void ClosePool(); //销毁所有连接

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_; //最大连接数
    int useCount_; //当前已使用的连接数
    int freeCount_; //当前空闲的连接数

    std::queue<MYSQL *> connQue_; //连接池
    std::mutex mtx_;
    sem_t semId_;
};


#endif // SQLCONNPOOL_H