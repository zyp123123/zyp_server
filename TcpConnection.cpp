#include "TcpConnection.h"
#include "EventLoop.h"
#include "Timer.h"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/sendfile.h>

TcpConnection::TcpConnection(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      channel_(loop, fd)
{
    channel_.setReadCallback(
        std::bind(&TcpConnection::handleRead, this));

    channel_.setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));

    std::cout << "TcpConnection fd=" << fd_
              << " created in thread "
              << std::this_thread::get_id() << std::endl;
}

TcpConnection::~TcpConnection()
{
    std::cout << "~TcpConnection fd=" << fd_ << std::endl;
    if (sendingFileFd_ != -1) {
        ::close(sendingFileFd_);
    }
    close(fd_);
}

void TcpConnection::connectEstablished()
{
    state_ = State::Connected;
    channel_.enableReading();
    refreshAlive();
}

void TcpConnection::handleRead()
{
    ssize_t n = inputBuffer_.readFd(fd_);
    if (n > 0) {
        
        refreshAlive(); // 这里的 refreshAlive 现在只负责确保有定时器在跑

        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_);
        }
    } else if (n == 0) {
        handleClose();
    }
}

void TcpConnection::handleClose()
{
    if (state_ == State::Disconnected) return;
    state_ = State::Disconnected;
    channel_.disableAll();

    // ⭐ 新增：如果正在发文件，连接断开了，必须关闭文件 fd
    if (sendingFileFd_ != -1) {
        ::close(sendingFileFd_);
        sendingFileFd_ = -1;
    }

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}


void TcpConnection::connectDestroyed()
{
    if (auto timer = aliveTimer_.lock()) {
        timer->cancel();
    }
    channel_.disableAll();
}


void TcpConnection::send(const std::string& data)
{
    if (loop_->isInLoopThread()) {
        sendInLoop(data);
    } else {
        loop_->runInLoop(
            std::bind(&TcpConnection::sendInLoop, this, data));
    }
}

void TcpConnection::sendInLoop(const std::string& data)
{
    // ⭐⭐⭐ 高水位检测
    if (pendingBytes() + data.size() > kHighWaterMark) {
        std::cout << "[HighWaterMark] pause reading fd=" << fd_ << std::endl;

        if (!readingPaused_) {
            channel_.disableReading();
            readingPaused_ = true;
        }

        // 极端策略：直接关闭（可选）
        // handleClose();
        return;
    }


    ssize_t nwrote = 0;

    // 1️⃣ 如果当前没有写事件 & buffer 为空，尝试直接写
    if (!channel_.isWriting() && outputBuffer_.readableBytes() == 0) {
        nwrote = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);
        if (nwrote >= 0) {
            if ((size_t)nwrote == data.size()) {
                return; // 全写完
            }
        } else {
            if (errno != EAGAIN) {
                perror("write");
                return;
            }
            nwrote = 0;
        }
    }

    // 2️⃣ 剩余数据进 outputBuffer
    size_t remaining = data.size() - nwrote;

    // ⭐ Backpressure：高水位检查
    // if (outputBuffer_.readableBytes() + remaining > kHighWaterMark) {
    //     std::cerr << "[Backpressure] output buffer overflow, close fd="
    //             << fd_ << std::endl;
    //     handleClose();
    //     return;
    // }

    outputBuffer_.append(data.data() + nwrote, remaining);

    // 3️⃣ 注册 EPOLLOUT
    if (!channel_.isWriting()) {
        channel_.enableWriting();
    }
}

void TcpConnection::sendFile(int fd, off_t offset, off_t size)
{
    /*if (pendingBytes() + size > kHighWaterMark) {
        ::close(fd);
        handleClose();
        return;
    }*/

    if (loop_->isInLoopThread()) {
        sendingFileFd_ = fd;
        sendingFileOffset_ = offset;
        sendingFileRemain_ = size;   // ⭐⭐ 关键
        channel_.enableWriting();
    } else {
        loop_->runInLoop(
            std::bind(&TcpConnection::sendFile, this, fd, offset, size));
    }
}


void TcpConnection::handleWrite() {
    // 1️⃣ 第一步：必须先保证 Buffer (Headers) 发完！
    if (outputBuffer_.readableBytes() > 0) {
        ssize_t n = ::write(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
        if (n > 0) {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() > 0) {
                return; // Buffer 还没发完，下次再说，不要进 sendfile
            }
        } else if (n == -1 && errno != EAGAIN) {
            handleClose();
            return;
        }
    }

    // 2️⃣ 第二步：Buffer 发完了，再发文件
    if (sendingFileFd_ != -1) {
        ssize_t n = ::sendfile(
            fd_,
            sendingFileFd_,
            &sendingFileOffset_,
            sendingFileRemain_
        );

        if (n > 0) {
            sendingFileRemain_ -= n;
            if (sendingFileRemain_ == 0) {
                ::close(sendingFileFd_);
                sendingFileFd_ = -1;
            }
            return;
        } else if (n == -1) {
            if (errno != EAGAIN) {
                perror("sendfile");
                handleClose();
            }
            return;
        }
    }


    // 3️⃣ 第三步：全部发完，关闭写监听
    if (outputBuffer_.readableBytes() == 0 && sendingFileFd_ == -1) {
        channel_.disableWriting();
        // ⭐⭐⭐ 恢复读
        if (readingPaused_) {
            channel_.enableReading();
            readingPaused_ = false;
        }

        if (state_ == State::Disconnecting) {
            shutdownInLoop();
        }
    }
}

void TcpConnection::setMessageCallback(MessageCallback cb)
{
    messageCallback_ = std::move(cb);
}

void TcpConnection::shutdown()
{
    if (loop_->isInLoopThread()) {
        shutdownInLoop();
    } else {
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_.isWriting()) {
        ::shutdown(fd_, SHUT_WR);
    } else {
        state_ = State::Disconnecting;
    }
}

void TcpConnection::refreshAlive()
{
    if (!loop_) return;

    // 1️⃣ 取消旧定时器
    if (auto old = aliveTimer_.lock()) {
        old->cancel();
    }

    // 2️⃣ 创建新定时器（弱引用）
    std::weak_ptr<TcpConnection> weakSelf = shared_from_this();

    auto timer = loop_->runAfter(kIdleTimeout, [weakSelf]() {
        if (auto conn = weakSelf.lock()) {
            conn->handleTimeout();
        }
    });

    aliveTimer_ = timer;
}

void TcpConnection::handleTimeout()
{
    if (state_ == State::Disconnected) return;

    std::cout << "⏰ Connection timeout, force closing fd=" << fd_ << std::endl;

    // ⭐ 这里必须直接调用 handleClose()
    // 它会执行：1. 改变状态 2. 停止 epoll 监控 3. 回调 TcpServer 移除自己
    handleClose();
}