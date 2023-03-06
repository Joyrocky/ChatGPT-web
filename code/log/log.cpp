/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#include "log.h"

using namespace std;

Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
}

Log::~Log() {
    if(writeThread_ && writeThread_->joinable()) {
        while(!deque_->empty()) {
            // 通知消费者线程退出wait()消耗堵塞队列的日志直到为空
            deque_->flush();
        };
        // 队列为空，关闭资源
        deque_->Close();
        //主线程等待异步写线程结束
        writeThread_->join();
    }

    // 关闭log的文件指针
    if(fp_) {
        lock_guard<mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

//可选择的参数有日志等级、日志路径、最大行数以及最长日志条队列
void Log::init(int level = 1, const char* path, const char* suffix,
    int maxQueueSize) {
    isOpen_ = true;
    level_ = level;

    //如果设置了max_queue_size,则设置为异步
    if(maxQueueSize > 0) {

        //设置写入方式为异步
        isAsync_ = true;
        if(!deque_) {
            // 创建堵塞队列
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            deque_ = move(newDeque);
            
            // 创建线程异步写日志
            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread));
            writeThread_ = move(NewThread);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;

    {
        lock_guard<mutex> locker(mtx_);
        // 清空buff并重置log的文件指针
        buff_.RetrieveAll();
        if(fp_) { 
            flush();
            fclose(fp_); 
        }

        // 打开文件指针
        fp_ = fopen(fileName, "a");
        if(fp_ == nullptr) {
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
        } 
        assert(fp_ != nullptr);
    }
}

//将输出内容按照标准格式整理
// write()函数完成写入日志文件中的具体内容，主要实现日志分级、分文件、格式化输出内容。
void Log::write(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;

    /* 日志日期 日志行数 */
    //日志不是今天或写入的日志行数是最大行的倍数
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_  %  MAX_LINES == 0)))
    {
        unique_lock<mutex> locker(mtx_);
        locker.unlock();    
        
        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};

        //格式化日志名中的时间部分
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        //如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (toDay_ != t.tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }
        else {
            //超过了最大行，在之前的日志名基础上加后缀,lineCount_  / MAX_LINES
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_  / MAX_LINES), suffix_);
        }
        
        locker.lock();
        flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        unique_lock<mutex> locker(mtx_);
        
        // 更新现有行数
        lineCount_++;

        //写入内容格式：时间 + 内容
        //时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的null字符
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
                    
        buff_.HasWritten(n);
        AppendLogLevelTitle_(level);

        //将传入的format参数赋值给valst，便于格式化输出
        va_start(vaList, format);
        //内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);

        //若m_is_async为true表示异步，默认为同步
        //若异步,则将日志信息加入阻塞队列,同步则加锁向文件中写  
        if(isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            fputs(buff_.Peek(), fp_);
        }
        // 清空Buff
        buff_.RetrieveAll();
    }
}

//日志分级
void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

void Log::flush() {
    if(isAsync_) { 
        deque_->flush(); 
    }
    // fflush(FILE *stream)会强迫将缓冲区内的数据写回参数stream 指定的文件中，如果参数stream 为NULL，fflush()会将所有打开的文件数据更新。
    fflush(fp_);
}

// 异步日志写入方法
void Log::AsyncWrite_() {
    string str = "";

    //从阻塞队列中取出一条日志内容，写入文件
    while(deque_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        // int fputs(const char *str, FILE *stream);
        // str，一个数组，包含了要写入的以空字符终止的字符序列。
        // stream，指向FILE对象的指针，该FILE对象标识了要被写入字符串的流。
        fputs(str.c_str(), fp_);
    }
}

// 局部静态变量之线程安全懒汉模式
Log* Log::Instance() {
    static Log inst;
    return &inst;
}


// 异步写线程
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}