#pragma once
#include <functional>
#include <chrono>

class Timer {
public:
    using TimerCallback = std::function<void()>;
    using TimePoint = std::chrono::steady_clock::time_point;

    Timer(TimePoint when, TimerCallback cb)
        : expiration_(when),    // 设置到期时间点
          callback_(std::move(cb)), // 设置回调函数
          cancelled_(false) {}  // 初始状态为未取消

    // 执行定时器回调
    void run() {
        if (!cancelled_) callback_();
    }

    // 取消该定时器（如果已到期但还没执行，可以拦截）
    void cancel() { cancelled_ = true; }

    // 获取到期时间
    TimePoint expiration() const { return expiration_; }

private:
    TimePoint expiration_;  //绝对到期时间（steady_clock）
    TimerCallback callback_;    //到点后执行的业务逻辑
    bool cancelled_;    // 取消标志位
};
