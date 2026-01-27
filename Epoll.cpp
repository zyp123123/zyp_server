#include "Epoll.h"
#include "Channel.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <cerrno>
#include <cstdio>

/**
 * 1024指一次 epoll_wait 最多接收 1024 个就绪事件，下面的events_.size()就等于1024
 * 如果同时有 2000 个 fd 就绪怎么办？
 * epoll_wait 只返回 1024 个,剩下的下次再返回剩下的,事件不会丢
 * */
Epoll::Epoll() : epfd_(epoll_create1(0)), events_(1024) {}

Epoll::~Epoll() {
    close(epfd_);
}

//从内核获取就绪事件，并翻译成一组已就绪的 Channel 对象
std::vector<Channel*> Epoll::poll(int timeout) {
    //阻塞当前线程,直到有fd事件发生或timeout到期,然后把事件写进events_数组,返回事件数量 n
    int n = epoll_wait(epfd_, events_.data(), events_.size(), timeout);
    std::vector<Channel*> active;

    for (int i = 0; i < n; ++i) {
        //从 updateChannel 中存进去的 channel 指针拿出来
        Channel* ch = static_cast<Channel*>(events_[i].data.ptr);
        //设置实际发生的事件
        ch->setRevents(events_[i].events);   
        active.push_back(ch);
    }
    return active;
}

//在 epoll 内核中建立：fd → epoll_event(data.ptr = Channel*) 的映射
void Epoll::updateChannel(Channel* channel)
{
    epoll_event ev{};
    ev.events = channel->events();
    //把 Channel 存进 epoll 内核*
    ev.data.ptr = channel;
    int fd = channel->fd();

    // 逻辑：先尝试添加，如果已存在则修改
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
        } else {
            perror("epoll_ctl add fatal error");
        }
    }
}

//从 epoll 中解除 “fd → Channel” 的映射
void Epoll::removeChannel(Channel* channel)
{
    epoll_ctl(epfd_, EPOLL_CTL_DEL, channel->fd(), nullptr);
}