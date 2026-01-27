#pragma once
#include <set>
#include <memory>
#include "Timer.h"

class EventLoop;
class Channel;

class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    // 添加定时器（由 EventLoop::runAfter 调用）
    std::shared_ptr<Timer>addTimer(const Timer::TimePoint& when,
            Timer::TimerCallback cb);

private:
    void handleRead();      // timerfd 报警后的读回调（核心分发逻辑）
    void resetTimerfd();    // 重新计算并设置 timerfd 的下次触发时间

private:
    EventLoop* loop_;   // 所属的事件循环
    int timerfd_;       // Linux 内核定时器文件描述符
    std::unique_ptr<Channel> timerfdChannel_;   // 封装 timerfd 的 Channel

    // 自定义比较规则：用于 std::set 排序
    struct TimerCmp {
        bool operator()(const std::shared_ptr<Timer>& a,
                        const std::shared_ptr<Timer>& b) const {
            // 首先按到期时间升序排（最早过期的排在 set 最前面）
            if (a->expiration() != b->expiration()) {
                return a->expiration() < b->expiration();
            }
            // 如果时间相同，按指针地址排，防止 set 认为两个定时器是同一个而无法插入
            return a.get() < b.get(); 
        }
    };
    // 存储定时器的红黑树，首个元素永远是最近要过期的
    std::set<std::shared_ptr<Timer>, TimerCmp> timers_;
};
