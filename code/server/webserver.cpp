/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    // 获取项目的运行路径
    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources", 16);
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;
    // 初始化数据库连接池
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 设置事件触发模式
    InitEventMode_(trigMode);
    // 初始化Socket连接
    if(!InitSocket_()) { isClose_ = true;}

    // 初始化日志系统
    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

// 关闭连接，释放内存和资源
WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

// 设置事件的触发模式
// events可以是下面几个宏的集合:
// EPOLLRDHUP 表示读关闭; EPOLLHUP 表示读写都关闭。
// EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
// EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。

/*
    epoll有两种工作模式：LT（水平触发）模式和ET（边缘触发）模式。
    默认情况下，epoll采用 LT模式工作，这时可以处理阻塞和非阻塞套接字，
    而EPOLLET表示可以将一个事件改为 ET模式。ET模式的效率要比 LT模式高，它只支持非阻塞套接字。
 */   

/*
    注意：epoll的两种触发模式：
	边沿触发vs水平触发
	epoll事件有两种模型，边沿触发：edge-triggered (ET)， 水平触发：level-triggered (LT)
	水平触发(level-triggered),是epoll的默认模式
		socket接收缓冲区不为空 有数据可读 读事件一直触发
		socket发送缓冲区不满 可以继续写入数据 写事件一直触发
	边沿触发(edge-triggered)
		socket的接收缓冲区状态变化时触发读事件，即空的接收缓冲区刚接收到数据时触发读事件
		socket的发送缓冲区状态变化时触发写事件，即满的缓冲区刚空出空间时触发读事件
	边沿触发仅触发一次，水平触发会一直触发。
	开源库:libevent 采用水平触发， nginx 采用边沿触发
*/
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    /* 针对connfd，开启EPOLLONESHOT，因为我们希望每个socket在任意时刻都只被一个线程处理 */
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        // 监听事件和连接事件都设置为ET
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    // 判断HTTP连接事件是否为ET模式
    HttpConn::isET = (connEvent_ & EPOLLET);
}

// 使用Reactor模式进行事件处理
// 主线程只负责监听文件描述符上是否有事件发生；有的话通知工作线程（线程池）,读写数据、接受新连接及处理客户请求均在工作线程中完成。
// 服务器通过epoll这种I/O复用技术（还有select和poll）来实现对监听socket（listenfd）和连接socket（客户请求）的同时监听
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {
        if(timeoutMS_ > 0) {
            // 关闭超时连接并返回距下一个超时连接的时间
            timeMS = timer_->GetNextTick();
        }
        // 在timeMS时间内将触发的事件写入events_数组中并返回事件的数目
        int eventCnt = epoller_->Wait(timeMS);

        // 主线程监听事件
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            // 事件表中就绪的socket文件描述符
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            
            // 当listen到新的用户连接，处理listenfd上产生的就绪事件
            if(fd == listenFd_) {
                DealListen_();
            }
            // EPOLLERR：表示对应的文件描述符发生错误；
            // EPOLLRDHUP 表示读关闭; EPOLLHUP 表示读写都关闭。
            // 发生EPOLLRDHUP | EPOLLHUP | EPOLLERR关闭连接

            // 如有异常，则直接关闭客户连接，并删除该用户的timer
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            // EPOLLIN事件产生的原因就是有新数据到来,此时服务端的socket可读
            // 发生EPOLLIN事件处理读事件

            /* 主线程从这一sockfd循环读取数据, 直到没有更多数据可读 */
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            // 发生EPOLLOUT事件处理写事件

             /* 当这一sockfd上有可写事件时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果 */
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

// 发生错误通知客户端并关闭连接
void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// 删除client对应epoll事件并关闭连接
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

/* 并将connfd注册到epoll事件表中 */
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    //connfd对应HTTP连接初始化
    users_[fd].init(fd, addr);
    if(timeoutMS_ > 0) {
        // 对socket连接设置定时器，绑定关闭连接的回调函数
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }

    // 将HTTP连接的fd及相关事件添加到epoll对象fd中；目的就是通过这个epoll对象来监视这个HTTP连接
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    // 设置为非堵塞状态
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}


// 本项目中Web服务器如何处理以及响应接收到的HTTP请求报文:
/*
    该项目使用线程池（Reactor模式）并发处理用户请求; 
    通常使用同步I/O模型（如epoll_wait）实现Reactor
    主线程只负责监听文件描述符上是否有事件(可读、可写)发生；有的话通知工作线程（线程池）,将socket可读可写事件放入请求队列，交给工作线程处理。
    
    我们将listenfd上到达的connection通过 accept()接收，并返回一个新的socket文件描述符connfd用于和用户通信，
    并对用户请求返回响应，同时将这个connfd注册到内核事件表中，等用户发来请求报文。

    这个过程是：首先将listenfd相关的事件注册到epoll对象中，并处理listenfd的就绪事件返回connfd，并将connfd注册到epoll对象中；当通过epoll_wait发现这个connfd上有可读事件了（EPOLLIN），主线程通知工作线程将socket可读事件放入请求队列，交给工作线程处理。
    [线程池的实现还需要依靠锁机制以及信号量机制来实现线程同步，保证操作的原子性。]

    当有空闲线程时取出读任务,将读任务的HTTP的请求报文读入其读缓存中，处理HTTP请求并生成响应报文放入写缓存中，并用epoll修改事件为写事件
    主线程检测到写事件后通知工作线程将可写事件放入请求队列；
    当有空闲线程时取出写任务,将写任务对应的HTTP连接的写缓存中服务器处理客户端请求的结果读入对应请求socket连接。
    若请求为长连接则继续监听读事件，否则断开socket连接。
*/

// 处理listenfd上的就绪事件
void WebServer::DealListen_() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        /* accept()返回一个新的socket文件描述符conndfd用于send()和recv() */
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        /* 并将connfd注册到epoll事件表中 */
        AddClient_(fd, addr);
    } while(listenEvent_ & EPOLLET); /* ET模式 */
}

// 线程池请求队列增加读任务
void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    // bind绑定读任务函数和参数列表
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

// 线程池请求队列增加写任务
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    // bind绑定写任务函数和参数列表
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 刷新HTTP连接事件的定时器时间
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    //从fd中读取数据到Buffer
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

// 处理HTTP请求并生成响应报文
void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {
        // 成功生成响应报文，修改已经注册的fd的监听事件为写事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {
        // 读缓冲没有内容；修改已经注册的fd的监听事件为读事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    //从Buffer写出数据到fd中
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            // 长连接继续监听读事件
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    //短连接发送完直接断开连接
    CloseConn_(client);
}

// 初始化Socket连接
/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;

    /* 创建监听socket的TCP/IP的IPV4 socket地址 */
    struct sockaddr_in addr;
    // 只能监听动态端口
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* INADDR_ANY：将套接字绑定到所有可用的接口 */
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };

    
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    /* 创建监听socket文件描述符 */
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    // SO_LINGER：延迟关闭连接
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    // SO_REUSERADDR：允许重用本地地址和端口
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    /* 绑定socket和它的地址 */
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    /* 创建监听队列以存放待处理的客户连接，在这些客户连接被accept()之前 */
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 将监听socket的fd及相关事件添加到epoll对象fd中；目的就是通过这个epoll对象来监视这个socket
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设文件描述符状态为非阻塞模式
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    // 设置非堵塞，作要么成功，要么立即返回错误，不被阻塞
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


