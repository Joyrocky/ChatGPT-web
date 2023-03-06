/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <sys/time.h>

template<class T>
// 阻塞队列类中封装了生产者-消费者模型
class BlockDeque {
public:
    explicit BlockDeque(size_t MaxCapacity = 1000);

    ~BlockDeque();

    void clear();

    bool empty();

    bool full();

    void Close();

    size_t size();

    size_t capacity();

    T front();

    T back();

    void push_back(const T &item);

    void push_front(const T &item);

    bool pop(T &item);

    bool pop(T &item, int timeout);

    void flush();

private:
    std::deque<T> deq_;  // 阻塞队列，共享变量

    size_t capacity_;

    std::mutex mtx_; // 互斥锁

    bool isClose_;

    // 条件变量实现多线程同步操作
    // 当条件不满足时，相关线程被一直阻塞，直到某种条件出现，这些线程才会被唤醒。
    std::condition_variable condConsumer_;

    std::condition_variable condProducer_;
};


template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) :capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    isClose_ = false;
}

template<class T>
BlockDeque<T>::~BlockDeque() {
    Close();
};

// 修改/获取共享变量deque的操作
// 1. 获得一个std::mutex 2. 当持有锁的时候，执行修改动作 3. 对std::condition_variable执行notify_one或notify_all(当做notify动作时，不必持有锁)
template<class T>
void BlockDeque<T>::Close() {
    {   
        std::lock_guard<std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    condProducer_.notify_all();
    condConsumer_.notify_all();
};


// notify_one()
// 解除阻塞当前正在等待此条件的线程之一。如果没有线程在等待，则还函数不执行任何操作。如果超过一个，不会指定具体哪一线程。
template<class T>
void BlockDeque<T>::flush() {
    // 当做notify动作时，不必持有锁
    // 通知消费者线程退出wait
    condConsumer_.notify_one();
};

template<class T>
void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear();
}

template<class T>
T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

template<class T>
T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

template<class T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

template<class T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}


// wait（unique_lock <mutex>＆lck）
// 当前线程的执行会被阻塞，直到收到 notify 为止。
// 放入元素到堵塞队列中
template<class T>
void BlockDeque<T>::push_back(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    // 队列中元素数量超过当前队列的容量，堵塞当前线程
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker);
    }
    deq_.push_back(item);
    condConsumer_.notify_one();
}

template<class T>
void BlockDeque<T>::push_front(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

// 检查堵塞队列是否为空
template<class T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

// 检查堵塞队列是否满
template<class T>
bool BlockDeque<T>::full(){
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

// 弹出队列中的元素
template<class T>
bool BlockDeque<T>::pop(T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    //pop时，如果当前队列没有元素,消费者线程将会等待条件变量
    while(deq_.empty()){
        condConsumer_.wait(locker);
        if(isClose_){
            return false;
        }
    }

    // 取出队首元素，并告知消费者线程退出wait()
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}


// 增加了超时处理，在项目中没有使用到
// 在pthread_cond_wait基础上增加了等待的时间，只指定时间内能抢到互斥锁即可
// 其他逻辑不变
template<class T>
bool BlockDeque<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()){
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) 
                == std::cv_status::timeout){
            return false;
        }
        if(isClose_){
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

#endif // BLOCKQUEUE_H