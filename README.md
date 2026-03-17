C++ Linux High-Performance Web Server

📖 项目介绍

本项目是一个基于 C++11 和 Linux OS API 构建的高性能、轻量级 Web 服务器。核心采用了 Main-Sub Reactor 网络模型 和 One Loop Per Thread 架构，全程无锁化设计。不仅支持基础的 HTTP/1.1 静态资源请求，
还完整实现了大文件流式上传、断点续传（Range）、以及高性能的异步双缓冲区日志系统。

本项目旨在深入探索和实践 Linux 网络编程、高并发架构设计以及现代 C++ 的高级特性。

🚀 核心功能

HTTP 协议栈完整实现：支持 GET / POST / HEAD 方法，支持 HTTP Keep-Alive 长连接机制。

大文件处理能力：支持基于 sendfile 的静态大文件零拷贝下载，支持 Range 断点续传；实现了基于状态机的大文件流式解析上传，极低内存占用。

高性能网络 I/O：基于 epoll I/O 多路复用机制，结合边缘/水平触发，实现高效的事件分发。

工业级异步日志：自研双缓冲区（Double Buffering）异步日志系统，支持按时间或按大小自动滚动（Roll），避免磁盘 I/O 阻塞网络线程。

健壮的连接管理：基于 timerfd 和红黑树（std::set）实现的高效定时器队列，精准剔除超时空闲连接。

🛠 技术栈

语言与标准：C++11/14 (充分利用 std::shared_ptr/weak_ptr、std::move、lambda、std::bind 等特性，确保内存安全)。

并发与线程：std::thread、std::mutex、std::condition_variable、线程池。

网络与 OS API：epoll、socket、sendfile、eventfd、timerfd、fcntl。

核心架构：Reactor 模式、状态机 (State Machine)。

💡 核心难点与技术突破 

1. 突破大文件上传的内存瓶颈 (流式状态机解析)

挑战：传统的 Web 框架在处理 multipart/form-data 上传时，往往先将请求体全部读入内存再解析，这在面对 GB 级别大文件时会导致服务器 OOM（内存溢出）。

解决方案：自研了 MultipartStreamParser 流式解析器。利用**有限状态机（FSM）**的原理，每次 epoll 触发读事件时，边读边找 boundary，将切分好的数据块直接通过 write 刷入磁盘文件。

成果：实现了内存占用的常量级控制。即使同时面临多个客户端并发上传 1GB+ 的视频文件，服务器内存波动依然平稳，兼顾了高并发与高稳定性。

2. 避免磁盘 I/O 阻塞网络线程 (异步双缓冲区日志)

挑战：在高并发场景下，如果 EventLoop 工作线程直接调用 write 写日志，一旦遇到磁盘 I/O 抖动，会瞬间导致网络请求卡死。

解决方案：设计并实现了基于双缓冲机制（Double Buffering）的 AsyncLogger。前端网络线程仅将日志拷贝到内存中的 currentBuffer_，写满后唤醒后台专属的日志归档线程。后台线程一次性将 Buffer 队列 swap 过去，再进行缓慢的磁盘落盘操作。

成果：前端记录日志的操作仅相当于一次内存拷贝，耗时降至纳秒级。同时实现了日志文件的自动分卷（按 10MB 大小或按天滚动），满足工业级可观测性需求。

3. 高效的大文件下载与背压控制 (零拷贝 + 高水位警戒)

挑战：大文件下载时，如果内核发送缓冲区被打满（返回 EAGAIN），程序无脑循环写会导致 CPU 空转；同时，多次在内核态与用户态之间拷贝文件数据极其消耗性能。

解决方案：

（1）引入 Linux sendfile 系统调用实现零拷贝，直接在内核态完成文件向 Socket 的搬运。

（2）设计了**高水位（High Water Mark）和背压（Backpressure）**机制。当发送缓冲区排队字节数超出阈值时，主动暂停该连接的 epoll 读事件监听；当内核可写时，利用 EPOLLOUT 事件触发 handleWrite 续传。

成果：极大降低了 CPU 占用率和上下文切换开销，保障了在极端网络拥塞情况下的服务器存活率。

4. 优雅的超时连接管理 ( timerfd 与红黑树)

挑战：HTTP 长连接（Keep-Alive）如果被客户端恶意占坑不发数据，会耗尽系统文件描述符（fd）。传统的后台轮询扫描方案效率极低。

解决方案：将“时间”也抽象为一种 I/O 事件。通过 Linux 的 timerfd 与 epoll 结合，将定时器唤醒统一接入 Reactor 循环。底层使用 std::set（红黑树）按过期时间排序管理所有定时任务，时间复杂度为 $O(\log N)$。并在 TCP 连接对象的生命周期管理中严格使用 std::weak_ptr 探活，防止野指针回调。

📂 目录结构与架构流转

├── Buffer.h/cpp           # 应用层读写缓冲区，支持动态扩容

├── Channel.h/cpp          # 封装 fd 与其关心的 epoll 事件，Reactor 核心组件

├── Epoll.h/cpp            # epoll 机制的 C++ 封装

├── EventLoop.h/cpp        # 事件循环核心，负责分发 I/O 事件与跨线程任务

├── EventLoopThread(Pool)  # sub-Reactor 线程池管理

├── HttpConnection.h/cpp   # HTTP 协议解析（状态机驱动）

├── HttpRouter.h/cpp       # 简单的轻量级路由分发器

├── MultipartStreamParser  # 核心流式表单解析器（专攻大文件上传）

├── TcpConnection.h/cpp    # TCP 连接的抽象，管理 Socket 生命周期

├── TcpServer.h/cpp        # 网络服务端入口，管理 Acceptor 与线程池

└── log/                   # 异步高并发日志系统

⚙️ 编译与运行

环境依赖：OS: Linux (Ubuntu 18.04/20.04+ 测试通过)

Compiler: g++ (支持 C++11 及以上)

Make

快速启动：Bash

# 1. 编译项目
make

# 2. 准备静态资源目录及配置文件
mkdir -p www/static

mkdir upload

echo "port=9999" > server.conf

echo "thread_num=4" >> server.conf

echo "static_path=./www/static" >> server.conf

# 3. 运行服务器
./reactor_server


打开浏览器访问：http://你的服务器IP:9999/ 即可看到文件列表页面，并支持文件预览、下载与大文件上传。
