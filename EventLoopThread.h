#pragma once
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>

class EventLoop;

class EventLoopThread {
public:
    EventLoopThread();
    ~EventLoopThread();

    //主线程调用：启动子线程，并在子线程创建好 Loop 后返回其指针
    EventLoop* startLoop();

private:
    //子线程的入口函数：负责创建 Loop 并开启循环
    void threadFunc();

    EventLoop* loop_;   // 指向子线程中创建的 EventLoop 对象
    std::thread thread_;    // 封装 C++11 线程对象

    std::mutex mutex_;  // 用于保护 loop_ 指针的初始化过程
    std::condition_variable cond_;  // 用于主、子线程间的同步（等待 Loop 创建完成）
};
