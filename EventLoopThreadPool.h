#pragma once
#include <vector>
#include <memory>

#include "EventLoopThread.h" 

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads);
    void start();   // 启动线程池

    EventLoop* getNextLoop();   // 核心函数：轮询获取一个子线程的 EventLoop

private:
    EventLoop* baseLoop_;   // 用户创建的主线程 Loop (MainLoop)
    int numThreads_;        // 线程池大小
    int next_;              // 轮询计数器

    // 存储所有的 EventLoopThread 对象
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    // 存储对应的 EventLoop 指针（方便快速获取）
    std::vector<EventLoop*> loops_;
};
