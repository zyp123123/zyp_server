#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
    : baseLoop_(baseLoop),
      numThreads_(numThreads),
      next_(0)
{
}

// 启动线程池
void EventLoopThreadPool::start()
{
    for (int i = 0; i < numThreads_; ++i) {
        // 1. 创建管理对象
        std::unique_ptr<EventLoopThread> t(new EventLoopThread());
        // 2. 启动子线程，主线程会在这里同步等待直到子线程的 Loop 创建成功
        EventLoop* loop = t->startLoop();
        // 3. 归档
        threads_.push_back(std::move(t));   // 将所有权转交给 vector
        loops_.push_back(loop);     // 记下 loop 指针
    }
}

//公平分配器
EventLoop* EventLoopThreadPool::getNextLoop()
{
    // 如果没有子线程，所有活都得主线程干
    if (loops_.empty()) {
        return baseLoop_;
    }

    // 轮询（Round-Robin）逻辑
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();    // 指针向后移一位，到头了就归零
    return loop;
}
