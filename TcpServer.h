#pragma once
#include <unordered_map>
#include <memory>
#include <functional>
#include "EventLoopThreadPool.h"
#include "HttpRouter.h"

class EventLoop;
class TcpConnection;
class Channel;

class TcpServer {
public:
    TcpServer(EventLoop* loop, int port);
    ~TcpServer();
    void start();   // 启动服务器

    HttpRouter& router() { return router_; }    // 获取 HTTP 路由对象

private:
    void handleAccept();    // 当 listenfd 有新连接进来时的回调
    void removeConnection(const std::shared_ptr<TcpConnection>& conn);  // 移除连接的回调

    EventLoop* loop_;   // MainLoop：只负责监听
    EventLoopThreadPool threadPool_;    // 线程池：管理工作线程（SubLoops）
    int listenfd_;  // 监听 Socket
    Channel* acceptChannel_;    // 封装 listenfd 的 Channel

    // 管理所有的连接：fd -> TcpConnection 指针
    std::unordered_map<int, std::shared_ptr<TcpConnection>> connections_;

    HttpRouter router_; // 应用层逻辑：HTTP 路由
};
