#pragma once
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include "Timer.h"

class TimerQueue;
class Channel;
class Epoll;

/**
 * EventLoop
 * =========
 * Reactor 模型中的“事件循环 / 调度核心”
 *
 * 职责：
 * 1. 在一个固定线程中循环调用 epoll_wait
 * 2. 分发 IO 事件给 Channel
 * 3. 执行跨线程投递的任务（Functor）
 * 4. 管理定时器（TimerQueue）
 * 5. 提供安全的跨线程唤醒机制
 */
class EventLoop {
public:
    // 通用回调类型：表示一个可执行任务
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 事件循环：Reactor 的核心执行函数
    void loop(); 

    // 请求退出事件循环（线程安全）   
    void quit();

    // 唤醒 epoll_wait（跨线程用）
    void wakeup();

    // Channel 状态变化时，同步到 epoll
    void updateChannel(Channel* channel);

    // 从 epoll 中移除 Channel
    void removeChannel(Channel* channel);  

    /* ========== 跨线程任务接口 ========== */
    // 如果在本线程，立即执行；否则投递到 EventLoop 线程
    void runInLoop(Functor cb);

    // 始终投递任务到 EventLoop 线程执行
    void queueInLoop(Functor cb);

    // 判断当前线程是否为 EventLoop 所在线程
    bool isInLoopThread() const;

    /* ========== 定时器接口 ========== */
    // delay 秒后执行回调
    std::shared_ptr<Timer>
    runAfter(double delay, Timer::TimerCallback cb);

private:
    // 执行所有投递到队列中的任务
    void doPendingFunctors();

    Epoll* poller_; // epoll 封装对象
    bool quit_;     // 事件循环退出标志

    /* ========== eventfd 唤醒机制 ========== */
    int wakeupFd_;  // 用于唤醒 epoll_wait 的 fd
    Channel* wakeupChannel_;    // 绑定 wakeupFd_ 的 Channel

    /* ========== 跨线程任务队列 ========== */
    std::mutex mutex_;  // 保护 pendingFunctors_
    std::vector<Functor> pendingFunctors_;
    std::thread::id threadId_;  // EventLoop 所在线程 ID

    /* ========== 定时器管理 ========== */
    std::unique_ptr<TimerQueue> timerQueue_;    
};
