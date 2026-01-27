#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include "TimerQueue.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <iostream>

//创建一个 可被 epoll 监听的 fd，用于从其他线程唤醒 EventLoop
static int createEventfd()
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (fd < 0) {
        perror("eventfd");  // 打印系统错误原因
        abort();    // 关键组件失败，直接终止程序
    }
    return fd;
}

EventLoop::EventLoop()
    : poller_(new Epoll()),
      quit_(false),
      wakeupFd_(createEventfd()),
      wakeupChannel_(nullptr),
      threadId_(std::this_thread::get_id()),
      timerQueue_(new TimerQueue(this))
{
    //绑定 wakeupFd_ 的 Channel
    wakeupChannel_ = new Channel(this, wakeupFd_);
    //设置唤醒回调
    wakeupChannel_->setReadCallback([this]() {
        uint64_t one;
        ssize_t n = read(wakeupFd_, &one, sizeof(one));
        (void)n;
    });
    //开始监听 wakeupFd_
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    removeChannel(wakeupChannel_);
    delete wakeupChannel_;
    close(wakeupFd_);
    delete poller_;
}

//完整的 Reactor 调度
void EventLoop::loop()
{
    while (!quit_) {
        // 1. 等待内核事件
        auto activeChannels = poller_->poll(1000); // 1 秒
        // 2. 分发IO事件
        for (auto ch : activeChannels) {
            ch->handleEvent();
        }
        // 3. 执行跨线程投递的任务
        doPendingFunctors();
    }
}

//线程安全退出
void EventLoop::quit()
{
    quit_ = true;
    /**
     * 为什么要 wakeup？
     * 如果 loop 正在 epoll_wait 阻塞,必须唤醒才能检查 quit_
    */
    wakeup(); 
}

//向 eventfd 写数据,epoll_wait 立即返回,触发 wakeupChannel_ 的读回调
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    (void)n;
}

//统一管理 Channel 与 epoll 的交互,Channel 不直接接触 epoll
void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}

/* ================= 核心：跨线程 ================= */
//能直接执行就直接执行,不能就跨线程去执行
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

//跨线程安全地投递任务,并唤醒 EventLoop
void EventLoop::queueInLoop(Functor cb)
{
    {
        // 1. 创建一个自动锁。离开大括号范围时，锁会自动释放。
        std::lock_guard<std::mutex> lock(mutex_);
        // 2. 将任务（回调函数）移动到待处理任务数组中。
        // std::move 是为了减少函数对象的拷贝开销，提高性能。
        pendingFunctors_.push_back(std::move(cb));
    }
    wakeup();
}

//判断当前线程是否安全直接操作 Reactor
bool EventLoop::isInLoopThread() const
{
    return std::this_thread::get_id() == threadId_;
}

//让 EventLoop 在 N 秒后，自动帮我执行一下 cb 任务
std::shared_ptr<Timer> EventLoop::runAfter(double delay, Timer::TimerCallback cb)
{
    /// 1. 获取当前时间 + 延迟时间 = 目标触发时间点
    auto when = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(delay * 1000));
    // 2. 将任务交给 TimerQueue。TimerQueue 通常也利用 epoll 监听 timerfd，
    // 到时间后会唤醒 EventLoop 触发回调。
    return timerQueue_->addTimer(when, std::move(cb));
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    {
        // 1. 关键：加锁并进行 swap
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);                    
    }

    // 2. 执行任务
    for (auto& fn : functors) {
        fn();
    }
}