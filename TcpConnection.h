#pragma once
#include <memory>
#include <functional>
#include <chrono>
#include "Channel.h"
#include "Buffer.h"

class EventLoop;
class Timer;

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;

    TcpConnection(EventLoop* loop, int fd);
    ~TcpConnection();

    void connectEstablished();
    void connectDestroyed();   // ⭐ 新增

    void setCloseCallback(CloseCallback cb) {
        closeCallback_ = std::move(cb);
    }

    int fd() const { return fd_; }
    EventLoop* getLoop() const { return loop_; }

    void send(const std::string& data);

    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*)>;

    void setMessageCallback(MessageCallback cb);
    
    void refreshAlive();
    void shutdown();

    void sendFile(int filefd, off_t offset, off_t size);

    void setContext(const std::shared_ptr<void>& ctx) {
        context_ = ctx;
    }

    template<typename T>
    std::shared_ptr<T> getContext() const {
        return std::static_pointer_cast<T>(context_);
    }

    static constexpr size_t kHighWaterMark = 64 * 1024 * 1024; // 64MB

private:
    void sendInLoop(const std::string& data);
    void handleWrite();

    void handleRead();
    void handleClose();

    void shutdownInLoop();

    enum class State {
        Connected,
        Disconnecting,
        Disconnected
    };

    State state_{State::Connected};

    EventLoop* loop_;
    int fd_;
    Channel channel_;
    Buffer inputBuffer_;
    Buffer outputBuffer_;

    CloseCallback closeCallback_;
    MessageCallback messageCallback_;

    std::weak_ptr<Timer> aliveTimer_;

    // 超时时间（秒）
    static constexpr double kIdleTimeout = 60.0;

    void handleTimeout();

    int sendingFileFd_{-1};
    off_t sendingFileOffset_{0};
    off_t sendingFileRemain_{0};

    std::shared_ptr<void> context_; 

    size_t pendingBytes() const {
        size_t bytes = outputBuffer_.readableBytes();
        if (sendingFileFd_ != -1) {
            bytes += sendingFileRemain_;
        }
        return bytes;
    }

    bool readingPaused_{false};
};
