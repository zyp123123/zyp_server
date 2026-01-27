#include "Channel.h"
#include "EventLoop.h"
#include <sys/epoll.h>

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0)
{
}

//它什么都没执行，把一个“将来要调用的函数”存了起来
void Channel::setReadCallback(EventCallback cb)
{
    readCallback_ = std::move(cb);
}

void Channel::setWriteCallback(EventCallback cb)
{
    writeCallback_ = std::move(cb);
}

//开始监听 fd 的可读事件
void Channel::enableReading()
{
    events_ |= EPOLLIN;
    loop_->updateChannel(this);  
}

//不再接收数据
void Channel::disableReading()
{
    events_ &= ~EPOLLIN;
    loop_->updateChannel(this);
}

//开始监听 fd 的可写事件
void Channel::enableWriting()
{
    events_ |= EPOLLOUT;
    loop_->updateChannel(this);
}

//写完数据后，关闭写事件监听
void Channel::disableWriting()
{
    events_ &= ~EPOLLOUT;
    loop_->updateChannel(this);
}

//查询Channel当前状态是否是正在写
bool Channel::isWriting() const
{
    return events_ & EPOLLOUT;
}

//查询Channel当前状态是否是正在读
bool Channel::isReading() const
{
    return events_ & EPOLLIN;
}

//Reactor 的事件分发器
void Channel::  handleEvent()
{
    if (revents_ & EPOLLIN) {   // 如果实际发生了读事件
        if (readCallback_) readCallback_(); // 执行预设的回调（比如读取数据）
    }

    if (revents_ & EPOLLOUT) {  // 如果实际发生了写事件
        if (writeCallback_) writeCallback_();   // 执行写回调（比如发送缓冲区数据）
    }
}

//将 fd 从 epoll 中彻底移除
void Channel::disableAll()
{
    events_ = 0;
    loop_->removeChannel(this);
}
