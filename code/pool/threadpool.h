/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>

//使用 std::condition_variable 的 wait 会把目前的线程 thread 停下来并且等候事件通知，而在另一个线程中可以使用 std::condition_variable 的 notify_one 或 notify_all 发送通知那些正在等待的事件，在多线程中经常使用
//在使用std::condition_variable时需要使用std::unique_lock而不应该使用std::lock_guard。
class ThreadPool {
public:
    // explicit防止构造函数发生隐式转换
    // make_shared函数的主要功能是在动态内存中分配一个对象并初始化它，返回指向此对象的shared_ptr;由于是通过shared_ptr管理内存
    explicit ThreadPool(size_t threadCount = 8): pool_(std::make_shared<Pool>()) {
            assert(threadCount > 0);
            //默认8线程
            for(size_t i = 0; i < threadCount; i++) {
                //Lambda表达式广义捕获 pool
                std::thread([pool = pool_] {
                    //unique_lock在构造时或者构造后（std::defer_lock）获取锁，在作用域范围内可以手动获取锁和释放锁
                    std::unique_lock <std::mutex> locker(pool->mtx);
                    //不停处理线程池中的请求队列
                    while(true) {
                        //请求队列不为空则执行任务
                        if(!pool->tasks.empty()) {
                            //move转换为右值
                            auto task = std::move(pool->tasks.front());
                            pool->tasks.pop();
                            locker.unlock();
                            task();
                            locker.lock();
                        }
                        //线程池已销毁,退出循环 
                        else if(pool->isClosed) break;
                        //wait：阻塞当前线程直到条件变量被唤醒
                        //请求队列为空线程阻塞
                        else pool->cond.wait(locker);   
                    }
                }).detach();
                //detach将当前线程对象所代表的执行实例与该线程对象分离，使得线程的执行可以单独进行。一旦线程执行完毕，它所分配的资源将会被释放。
            }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool&&) = default;
    
    //销毁线程池
    ~ThreadPool() {
        if(static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed = true;
            }
            //notify_all：通知所有正在等待的线程
            //通知所有线程closed
            pool_->cond.notify_all();
        }
    }

    template<class F>
    //形参是右值引用
    //向线程池中提交一个任务
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);
            //请求队列增加任务
            //forward保持右值引用不变
            pool_->tasks.emplace(std::forward<F>(task));
        }
        //notify_one：通知一个正在等待的线程
        //唤醒一个被阻塞进程
        pool_->cond.notify_one();
    }

private:
    struct Pool {
        std::mutex mtx; //保护请求队列的互斥锁
        std::condition_variable cond; // 条件变量.
        bool isClosed;
        //function<int(int,int)>函数闭包，主要用于包装模板函数，此处包装void函数
        std::queue<std::function<void()>> tasks; //请求队列
    };
    std::shared_ptr<Pool> pool_;
};


#endif //THREADPOOL_H