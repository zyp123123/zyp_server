#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread()
    : loop_(nullptr)
{
}

EventLoopThread::~EventLoopThread()
{
    if (loop_) {
        loop_->quit();   //1. 优雅通知子线程的 EventLoop 退出
    }
    /*在 C++ 中，std::thread 对象在销毁时，必须处于 "Joined"（已回收）或 "Detached"（已分离）状态，
    否则程序会直接崩溃（抛出 std::terminate）。*/
    //需要主线程等子线程结束完成后才能结束，防止非法内存访问
    if (thread_.joinable()) {
        thread_.join();     // 2. 回收子线程资源
    }
}

EventLoop* EventLoopThread::startLoop()
{
    // 1. 启动系统线程，并绑定执行 threadFunc
    thread_ = std::thread(&EventLoopThread::threadFunc, this);

    // 2. 关键同步点：主线程不能立刻返回 loop_，因为此时 threadFunc 可能还没创建好 Loop
    std::unique_lock<std::mutex> lock(mutex_);
    // 3. 等待子线程通知。直到 loop_ 不为 nullptr 时才继续往下走
    cond_.wait(lock, [this]() {
        return loop_ != nullptr;
    });

    return loop_;   // 此时 loop_ 保证已经被子线程初始化好了
}

void EventLoopThread::threadFunc()
{
    // --- 运行在子线程中 ---

    // 1. 在子线程栈（或堆）上创建 EventLoop
    // 每个线程只能有一个 EventLoop，这里创建后，该 Loop 就属于这个新线程
    EventLoop* loop = new EventLoop();   //必须 new

    {
        // 2. 加锁赋值给成员变量 loop_，并通知主线程“我已经准备好了”
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = loop;   // 赋值给成员变量
        cond_.notify_one(); // 敲门：唤醒主线程
    }

    // 3. 开启 Reactor 循环：代码会阻塞在这里，不断 poll 事件
    loop->loop();

    // 4. 当退出循环后，释放内存
    delete loop;   // loop 退出后再释放
}
