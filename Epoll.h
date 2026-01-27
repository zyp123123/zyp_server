#pragma once
#include <sys/epoll.h>
#include <vector>

class Channel;

/**
 * Epoll 是对 Linux epoll 的封装
 * 负责：
 *  - 管理 epoll_fd
 *  - 将 Channel 注册 / 更新 / 移除到 epoll
 *  - 调用 epoll_wait 获取活跃事件
 */
class Epoll {
public:
    Epoll();
    ~Epoll();

    // 调用 epoll_wait，返回当前发生事件的 Channel 列表
    std::vector<Channel*> poll(int timeout);

    // 根据 Channel 当前的 events_，将其添加或修改到 epoll 中
    void updateChannel(Channel* channel);

    // 将 Channel 对应的 fd 从 epoll 中移除
    void removeChannel(Channel* channel);

private:
    // 内核 epoll 实例
    int epfd_;

    // epoll_wait 返回的事件数组（复用，避免频繁分配）
    std::vector<epoll_event> events_;
};
