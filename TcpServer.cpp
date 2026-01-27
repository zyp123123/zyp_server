#include "TcpServer.h"
#include "EventLoop.h"
#include "Channel.h"
#include "TcpConnection.h"
#include "HttpConnection.h"

#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>

static void setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

TcpServer::TcpServer(EventLoop* loop, int port)
    : loop_(loop),
      threadPool_(loop, 4),  // 初始化线程池，开启 4 个工作线程
      listenfd_(socket(AF_INET, SOCK_STREAM, 0))    //创建监听 socket
{
    // SO_REUSEADDR：防止服务器重启时出现 Address already in use 错误
    int opt = 1;
    setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    setNonBlocking(listenfd_);  // 必须设为非阻塞，因为这是边缘触发或高并发的基础
    bind(listenfd_, (sockaddr*)&addr, sizeof(addr));
    listen(listenfd_, SOMAXCONN);

    // 创建监听 Channel，绑定到 MainLoop
    acceptChannel_ = new Channel(loop_, listenfd_);
    // 当 listenfd 有读事件（新连接）时，执行 handleAccept
    acceptChannel_->setReadCallback(std::bind(&TcpServer::handleAccept, this));
}

TcpServer::~TcpServer()
{
    close(listenfd_);
    delete acceptChannel_;
}

void TcpServer::handleAccept()
{
    while (true) {  // 循环 accept 尽可能处理掉所有排队的连接
        sockaddr_in client{};
        socklen_t len = sizeof(client);

        int connfd = accept(listenfd_, (sockaddr*)&client, &len);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)    
                break;  // 处理完了，退出循环
            perror("accept");
            break;
        }

        setNonBlocking(connfd); // 新连接也要设为非阻塞

        // 关键点：从线程池中轮询拿一个 SubLoop（子线程）
        EventLoop* ioLoop = threadPool_.getNextLoop();
        // 创建 TcpConnection 对象，并将其交给选中的 ioLoop 管理
        auto conn = std::make_shared<TcpConnection>(ioLoop, connfd);
        // 绑定应用层协议（HttpConnection）
        auto http = std::make_shared<HttpConnection>(conn, &router_);

        // 绑定到 TcpConnection 生命周期
        conn->setContext(http); // 将 Http 逻辑存入 Tcp 连接中（方便后续取出）

        // 设置消息回调：当这个连接有数据来时，去调用 Http 的处理逻辑
        conn->setMessageCallback(
            [](const std::shared_ptr<TcpConnection>& conn, Buffer* buf) {
                auto http = conn->getContext<HttpConnection>();
                http->onMessage(buf);
            });

        // 设置断开回调
        conn->setCloseCallback(
            std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

        // 将连接加入大管家的记录本（map）中，防止其被销毁
        connections_[connfd] = conn;

        // 跨线程调用：让 SubLoop 去执行连接建立后的初始化动作
        ioLoop->runInLoop([conn]() {
            conn->connectEstablished();
        });
    }
}

void TcpServer::start()
{
    // 1. 业务逻辑准备：在启动前把路由规则（路径与回调的映射）填好
    // 比如：访问 /hello 执行打印，访问 /static 执行发文件
    router_.addRoute("GET", "/hello",
        [](const HttpRequest& req, HttpResponse& resp) {
            resp.setStatus(200, "OK");
            resp.setHeader("Content-Type", "text/plain");
            resp.setBody("Hello Router\n");
        });

    router_.addRoute("GET", "/static/test.bin",
        [](const HttpRequest& req, HttpResponse& resp) {
            std::string filepath = "./www/static/test.bin";

            int fd = ::open(filepath.c_str(), O_RDONLY);
            if (fd < 0) {
                resp.setStatus(404, "Not Found");
                return;
            }

            struct stat st;
            fstat(fd, &st);

            resp.setStatus(200, "OK");
            resp.setHeader("Content-Length", std::to_string(st.st_size));
            resp.setHeader("Content-Type", "application/octet-stream");

            resp.setSendFile(fd, st.st_size);
        });

    // 2. 启动子线程池：
    // 这会调用 EventLoopThreadPool::start()，
    // 真正创建 numThreads_ 个系统线程，并让它们全部阻塞在各自的 epoll_wait 上待命。
    threadPool_.start();   
    // 3. 开启监听：
    // 将 acceptChannel_（负责监听新连接的 fd）注册到 MainLoop 的 Epoll 中。
    // 从这一刻起，服务器正式对外开放，开始接收客户端的 SYN 包。
    acceptChannel_->enableReading();
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
    // 跨线程回到主线程处理，因为 connections_ map 是非线程安全的
    loop_->runInLoop([this, conn]() {
        connections_.erase(conn->fd()); // 从记录本删除

        EventLoop* ioLoop = conn->getLoop();    // 找到那个管理此连接的子线程
        // 再次跨线程，通知子线程销毁该连接
        ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    });
}