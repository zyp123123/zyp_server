#pragma once
#include <functional>

class EventLoop;

class Channel {
public:
    //一个可以被调用的函数对象，不需要参数，不需要返回值
    using EventCallback = std::function<void()>;

    // Channel 表示一个 fd 在 EventLoop 中的事件代理
    Channel(EventLoop* loop, int fd);

    // 设置读 / 写事件发生时的回调函数
    void setReadCallback(EventCallback cb);
    void setWriteCallback(EventCallback cb);

    // 开启 / 关闭对读写事件的关注（会同步更新到 epoll）
    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();

    // 查询当前是否关注读 / 写事件
    bool isWriting() const;
    bool isReading() const;
    
    // 基本属性访问
    int fd() const { return fd_; }
    int events() const { return events_; }

    // 由 EventLoop 在 epoll_wait 返回后设置
    void setRevents(int revt) { revents_ = revt; }

    // 根据 revents_ 分发事件，调用对应回调（Reactor 核心）
    void handleEvent();

    // 取消所有事件关注，并从 epoll 中移除该 fd
    void disableAll();

private:
    EventLoop* loop_;   // 所属 EventLoop（非常关键）
    int fd_;    // 被监听的文件描述符
    int events_;    // 我关心的事件（EPOLLIN | EPOLLOUT）
    int revents_;   // 实际发生的事件（epoll 返回）
    
    EventCallback readCallback_;    // 可读事件回调
    EventCallback writeCallback_;   // 可写事件回调
};
