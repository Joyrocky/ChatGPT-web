/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 
#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;

HttpConn::HttpConn() { 
    fd_ = -1;
    addr_ = { 0 };
    isClose_ = true;
};

HttpConn::~HttpConn() { 
    Close(); 
};

//HTTP连接初始化
void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    userCount++;
    addr_ = addr;
    fd_ = fd;
    //清空缓存
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

void HttpConn::Close() {
    // 释放内存
    response_.UnmapFile();
    if(isClose_ == false){
        isClose_ = true; 
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

//获取socket文件描述符fd
int HttpConn::GetFd() const {
    return fd_;
};

//获取socket的TCP/IP的IPV4 socket地址 
struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

//获取socket地址的IP
const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}

//获取socket地址的端口
int HttpConn::GetPort() const {
    return addr_.sin_port;
}

//从fd中读取数据到Buffer
ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        //使用Buffer封装的ReadFd接口
        len = readBuff_.ReadFd(fd_, saveErrno);
        //读取数据出错
        if (len <= 0) {
            break;
        }
    } while (isET); //先读取，再判断是否为ET模式（边缘触发模式）
    //ET模式：epoll_wait检测到有fd事件发生，立即进行处理，并将数据一次性读完
    return len;
}

//从Buffer写出数据到fd中
ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 将iovec中的数据发送给fd_
        len = writev(fd_, iov_, iovCnt_);
        if(len <= 0) {
            *saveErrno = errno;
            break;
        }

        // struct iovec {
        //     ptr_t iov_base; /* Starting address */
        //     size_t iov_len; /* Length in bytes */
        // };

        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; } /* 传输结束 */
        //传输数据超过第一个缓冲区的大小，更新第二个缓冲区的起始地址和剩余大小
        else if(static_cast<size_t>(len) > iov_[0].iov_len) {
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len) {
                //将writeBuf中所有字节读完
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            //长度小于第一个缓冲区的大小，则更新第二个缓冲区起始地址和剩余大小
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
            iov_[0].iov_len -= len; 
            //读取writeBuf中len个字节
            writeBuff_.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240); //ET模式且缓冲区大小>10240
    return len;
}

//处理请求并生成响应报文
bool HttpConn::process() {
    request_.Init();
    if(readBuff_.ReadableBytes() <= 0) {
        return false;
    }
    //解析缓冲区的报文内容
    else if(request_.parse(readBuff_)) {
        LOG_DEBUG("%s", request_.path().c_str());
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    } else {
        response_.Init(srcDir, request_.path(), false, 400);
    }

    // 生成响应报文到报文缓冲区中
    response_.MakeResponse(writeBuff_);
    /* 响应头 */
    // 第一个iovec指针指向响应报文缓冲区
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    /* 文件 */
    //第二个iovec指针指向mmap返回的文件指针
    if(response_.FileLen() > 0  && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;
}
