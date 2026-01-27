#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <fstream>

namespace logging {

class AsyncLogger {
public:
    explicit AsyncLogger(const std::string& filename,
                         size_t rollSize = 10 * 1024 * 1024,
                         int maxHistory = 7);
    ~AsyncLogger();

    void append(const char* data, size_t len);
    void start();
    void stop();
    void notify();

private:
    void threadFunc();
    void rollFileIfNeeded();
    void rollFileByDate();
    void cleanOldFiles();

private:
    const std::string filename_;
    const size_t rollSize_;
    const int maxHistory_;

    size_t writtenBytes_{0};
    std::ofstream ofs_;
    std::string curDate_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex mutex_;
    std::condition_variable cond_;

    using Buffer = std::string;
    using BufferPtr = std::unique_ptr<Buffer>;

    BufferPtr currentBuffer_;
    BufferPtr nextBuffer_;
    std::vector<BufferPtr> buffers_;
};

} // namespace logging
