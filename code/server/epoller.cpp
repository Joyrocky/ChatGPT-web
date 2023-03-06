/*
 * @Author       : mark
 * @Date         : 2020-06-19
 * @copyleft Apache 2.0
 */

#include "epoller.h"

// int epoll_create(int size)
// 内核会产生一个epoll 实例数据结构并返回一个文件描述符fd；size用来告诉内核这个监听的数目一共有多大。
Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){
    assert(epollFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller() {
    close(epollFd_);
}

// EPOLL_CTL_ADD：注册新的fd到epfd中；
bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;

    // int epoll_ctl(int epfd， int op， int fd， struct epoll_event *event)
    // 第1个参数 epfd是 epoll的描述符。
    // 第二个参数表示动作; 第三个参数是需要监听的fd; 第四个参数是告诉内核需要监听什么事
    // 把一个socket以及这个socket相关的事件添加到这个epoll对象描述符中去，目的就是通过这个epoll对象来监视这个socket【客户端的TCP连接】上数据的来往情况；当有数据来往时，系统会通知我们
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

// EPOLL_CTL_MOD：修改已经注册的fd的监听事件；
bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

// EPOLL_CTL_DEL：从epfd中删除一个fd；
bool Epoller::DelFd(int fd) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

// int epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout);
// 第1个参数 epfd是 epoll的描述符。第2个参数 events则是分配好的 epoll_event结构体数组，epoll将会把发生的事件复制到 events数组中
// 第3个参数 maxevents表示本次可以返回的最大事件数目; 第4个参数 timeout表示在没有检测到事件发生时最多等待的时间
// 等待事件的产生，返回事件的数目，并将触发的事件写入events_数组中。
// 处于ready状态的那些文件描述符会被复制进ready list中，epoll_wait用于向用户进程返回ready list。
int Epoller::Wait(int timeoutMs) {
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}