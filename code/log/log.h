/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "blockqueue.h"
#include "../buffer/buffer.h"


// 日志系统大致可以分成两部分，其一是单例模式与阻塞队列的定义，其二是日志类的定义与使用。

// 日志类的定义
// 通过局部变量的懒汉单例模式创建日志实例，对其进行初始化生成日志文件后，此时写入方式为异步则须创建堵塞队列和异步写线程
// 生成格式化日志内容，并根据写入方式将日志信息异步加入堵塞队列或同步写入文件
// 异步写入方式还需从堵塞队列中取出日志写入文件中

class Log {
public:

    // 初始化日志文件方法
    void init(int level, const char* path = "./log", 
                const char* suffix =".log",
                int maxQueueCapacity = 1024);

    // 公有实例获取方法
    static Log* Instance();

    //异步写日志公有方法，内部调用私有异步方法AsyncWrite_
    static void FlushLogThread();

    //将输出内容按照标准格式整理
    void write(int level, const char *format,...);

    //强制刷新缓冲区
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen() { return isOpen_; }
    
private:
    Log();
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    const char* path_;
    const char* suffix_;

    int MAX_LINES_; //日志最大行数

    int lineCount_; //日志行数记录
    int toDay_; //按天分文件,记录当前时间是那一天

    bool isOpen_;
 
    Buffer buff_; //要输出的内容
    int level_;
    bool isAsync_; //是否同步标志位

    FILE* fp_; //打开log的文件指针
    std::unique_ptr<BlockDeque<std::string>> deque_; //阻塞队列
    std::unique_ptr<std::thread> writeThread_; //异步写线程
    std::mutex mtx_;
};


// 内容格式化方法
#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
// 对日志等级进行分类，包括DEBUG，INFO，WARN和ERROR四种级别的日志

// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，这里printf参数列表中的的##会把前面多余的","去掉，否则会编译出错，建议使用后面这种，使得程序更加健壮。
#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif //LOG_H