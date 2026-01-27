#include "AsyncLogger.h"
#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <utility>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>

namespace logging {

// C++11 compatible make_unique
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

AsyncLogger::AsyncLogger(const std::string& filename, size_t rollSize, int maxHistory)
    : filename_(filename),
      rollSize_(rollSize),
      maxHistory_(maxHistory)
{
    currentBuffer_ = make_unique<Buffer>();
    nextBuffer_ = make_unique<Buffer>();
    currentBuffer_->reserve(4096);
    nextBuffer_->reserve(4096);

    // 当前日期
    time_t t = time(nullptr);
    tm tm_now;
    localtime_r(&t, &tm_now);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm_now);
    curDate_ = buf;
}

AsyncLogger::~AsyncLogger() {
    stop();
}

void AsyncLogger::start() {
    running_ = true;
    thread_ = std::thread(&AsyncLogger::threadFunc, this);
}

void AsyncLogger::stop() {
    if (running_) {
        running_ = false;
        cond_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
}

void AsyncLogger::append(const char* data, size_t len)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (currentBuffer_->size() + len < currentBuffer_->capacity()) {
        currentBuffer_->append(data, len);
    } else {
        buffers_.push_back(std::move(currentBuffer_));

        if (nextBuffer_) {
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            currentBuffer_ = make_unique<Buffer>();
            currentBuffer_->reserve(4096);
        }

        currentBuffer_->append(data, len);
        cond_.notify_one();
    }
}

// 按文件大小滚动
void AsyncLogger::rollFileIfNeeded()
{
    if (!ofs_.is_open()) return;

    if (writtenBytes_ >= rollSize_) {
        ofs_.close();
        
        char timebuf[64];
        // 获取当前时间点
        auto now = std::chrono::system_clock::now();
        // 获取毫秒部分
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;
        // 转换为 time_t 以获取年月日时分秒
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_r(&t, &tm_now);
        
        // 正确的 snprintf 格式化：使用 %d 而不是 %Y%m%d
        std::snprintf(timebuf, sizeof(timebuf), ".%04d%02d%02d-%02d%02d%02d.%03d", 
                      tm_now.tm_year + 1900, 
                      tm_now.tm_mon + 1, 
                      tm_now.tm_mday,
                      tm_now.tm_hour, 
                      tm_now.tm_min, 
                      tm_now.tm_sec, 
                      static_cast<int>(ms.count()));
        
        std::string newName = filename_ + timebuf;
        
        // 执行重命名：将 server.log 改名为 server.log.20251223-101407.123
        ::rename(filename_.c_str(), newName.c_str());

        // 重新打开空的 server.log
        ofs_.open(filename_, std::ios::app);
        writtenBytes_ = 0;

        cleanOldFiles();
    }
}

// 按日期滚动
void AsyncLogger::rollFileByDate()
{
    time_t t = time(nullptr);
    tm tm_now;
    localtime_r(&t, &tm_now);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm_now);
    std::string today = buf;

    if (today != curDate_) {
        ofs_.close();
        std::string newName = filename_ + "." + curDate_;
        ::rename(filename_.c_str(), newName.c_str());
        ofs_.open(filename_, std::ios::app);
        curDate_ = today;

        cleanOldFiles();
    }
}

// 删除过期日志文件
void AsyncLogger::cleanOldFiles()
{
    std::string dir = ".";
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;

    std::vector<std::string> logFiles;
    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string fname = entry->d_name;
        if (fname.find(filename_ + ".") == 0) {
            logFiles.push_back(fname);
        }
    }
    closedir(dp);

    if ((int)logFiles.size() <= maxHistory_) return;

    std::sort(logFiles.begin(), logFiles.end());
    int toRemove = logFiles.size() - maxHistory_;
    for (int i = 0; i < toRemove; ++i) {
        ::remove(logFiles[i].c_str());
    }
}

void AsyncLogger::threadFunc()
{
    ofs_.open(filename_, std::ios::app);
    writtenBytes_ = 0;

    BufferPtr newBuffer1 = make_unique<Buffer>();
    BufferPtr newBuffer2 = make_unique<Buffer>();
    newBuffer1->reserve(4096);
    newBuffer2->reserve(4096);

    std::vector<BufferPtr> buffersToWrite;

    while (running_ || !buffers_.empty()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait_for(lock, std::chrono::seconds(3), [this]() {
                return !buffers_.empty() || !running_;
            });

            if (!currentBuffer_->empty()) {
                buffers_.push_back(std::move(currentBuffer_));
            }

            buffersToWrite.swap(buffers_);

            if (!currentBuffer_) {
                if (newBuffer1) {
                    currentBuffer_ = std::move(newBuffer1);
                } else {
                    currentBuffer_ = make_unique<Buffer>();
                    currentBuffer_->reserve(4096);
                }
            }

            if (!newBuffer1) {
                newBuffer1 = make_unique<Buffer>();
                newBuffer1->reserve(4096);
            }
            if (!newBuffer2) {
                newBuffer2 = make_unique<Buffer>();
                newBuffer2->reserve(4096);
            }

            if (!nextBuffer_) {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        // 写入批量缓冲
        for (const auto& buffer : buffersToWrite) {
            if (buffer) {
                ofs_ << *buffer;
                writtenBytes_ += buffer->size();
            }
        }
        ofs_.flush();

        rollFileIfNeeded();
        rollFileByDate();

        buffersToWrite.clear();
    }
}

void AsyncLogger::notify()
{
    cond_.notify_one();
}
} // namespace logging
