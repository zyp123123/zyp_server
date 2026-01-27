#include "TimerQueue.h"
#include "EventLoop.h"
#include "Channel.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
    // 创建内核定时器 fd：CLOCK_MONOTONIC 单调时钟，不受系统修改时间影响
      timerfd_(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      timerfdChannel_(new Channel(loop, timerfd_))
{
    // 设置 Channel 的回调：当时间到时，内核会让 timerfd 可读，触发 handleRead
    timerfdChannel_->setReadCallback(std::bind(&TimerQueue::handleRead, this));
    timerfdChannel_->enableReading();   // 注册到 epoll
}

TimerQueue::~TimerQueue() {
    ::close(timerfd_);
}

//添加新的定时任务
std::shared_ptr<Timer> TimerQueue::addTimer(const Timer::TimePoint& when, 
                                            Timer::TimerCallback cb)
{
    auto timer = std::make_shared<Timer>(when, std::move(cb));
    // 插入 set，set 会根据 TimerCmp 自动排序
    timers_.insert(timer);

    /**
      * 如果插入的这个定时器是目前最早到期的（排在 set 开头）
      * 那么需要立即更新内核 timerfd 的触发时间，否则会导致定时器不准
      * 假设当前 set 里最早的任务是 10 秒后。你告诉内核：“10 秒后叫我”，
      * 此时你突然添加了一个 2 秒后的任务，变成了 set 的第一个，如果不重置内核闹钟，
      * 内核依然会在 10 秒后才响。那么这个 2 秒的任务就被“憋”了 8 秒才执行，定时器就不准了
     */
    if (timers_.begin() == timers_.find(timer)) {
        resetTimerfd();
    }
    return timer;
}

//定时器到期后执行回调函数
void TimerQueue::handleRead()
{
    uint64_t exp;
    // 必须 read timerfd，否则 epoll 会一直触发（电平触发模式）
    ssize_t n = ::read(timerfd_, &exp, sizeof(exp));
    (void)n;

    auto now = std::chrono::steady_clock::now();    //获取当前时间

    std::vector<std::shared_ptr<Timer>> expired;    //存放过期任务
    // 1. 寻找所有已过期的定时器
    for (auto it = timers_.begin(); it != timers_.end(); ) {
        if ((*it)->expiration() <= now) {   //已经过期
            expired.push_back(*it); // 加入过期列表
            it = timers_.erase(it); // 从集合中移除
        } else {
            break;  // 后面的一定还没到期，直接跳出
        }
    }
    // 2. 执行过期定时器的回调函数
    for (auto& timer : expired) {
        timer->run();
    }
    // 3. 处理完后，根据 set 中剩下的任务更新下一次内核触发时间
    resetTimerfd();
}

//重新锚定内核闹钟的响铃时刻
void TimerQueue::resetTimerfd()
{
    if (timers_.empty()) return;    //没有定时任务直接返回

    // 取出 set 中最早的任务时间点
    auto nextExpire = (*timers_.begin())->expiration(); 
    auto now = std::chrono::steady_clock::now();    //获取当前时间
    // 计算距离现在的纳秒差
    auto diff = nextExpire > now ?
        std::chrono::duration_cast<std::chrono::nanoseconds>(nextExpire - now) :
        std::chrono::nanoseconds(0);

    struct itimerspec newValue;
    memset(&newValue, 0, sizeof(newValue));
    // 设置下一次触发的时间（相对时间）
    newValue.it_value.tv_sec = diff.count() / 1000000000;
    newValue.it_value.tv_nsec = diff.count() % 1000000000;
    // 调用系统调用设置内核定时器
    ::timerfd_settime(timerfd_, 0, &newValue, nullptr);
}

