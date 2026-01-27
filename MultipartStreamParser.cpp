#include "MultipartStreamParser.h"
#include <cstring>
#include <sys/stat.h>

void MultipartStreamParser::onData(const char* data, size_t len) {
    buffer_.append(data, len);

    while (true) {
        if (state_ == State::SCAN_BOUNDARY) {
            // 寻找 boundary
            size_t pos = buffer_.find(boundary_);
            if (pos == std::string::npos) {
                // 保留 boundary 长度的后缀，防止切断 boundary，其余丢弃或报错（初始阶段不该有数据）
                if (buffer_.size() > boundary_.size()) {
                     buffer_.erase(0, buffer_.size() - boundary_.size());
                }
                return;
            }
            // 找到了 boundary
            buffer_.erase(0, pos + boundary_.size());
            
            // 检查是开始还是结束
            if (buffer_.size() < 2) return; // 等待更多数据
            
            if (buffer_.substr(0, 2) == "--") {
                state_ = State::DONE;
                buffer_.clear();
                return;
            }
            if (buffer_.substr(0, 2) == "\r\n") {
                buffer_.erase(0, 2);
                state_ = State::READ_HEADERS;
            }
        }

        if (state_ == State::READ_HEADERS) {
            size_t pos = buffer_.find("\r\n\r\n");
            if (pos == std::string::npos) return;

            currentHeaders_ = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 4);
            processHeader(); // 解析 headers，决定是文件还是字段
            state_ = State::READ_DATA;
        }

        if (state_ == State::READ_DATA) {
            // 在数据中寻找下一个 boundary
            // 注意：数据可能包含了 boundary 的一部分，所以不能轻易把 buffer 写空
            size_t pos = buffer_.find(boundary_);
            
            if (pos == std::string::npos) {
                // 没找到 boundary，说明 buffer 里大部分都是数据
                // 但要小心 boundary 被截断在末尾
                if (buffer_.size() > boundary_.size() + 4) {
                    size_t writeLen = buffer_.size() - boundary_.size() - 4;
                    if (isFile_ && fileFd_ >= 0) {
                        ssize_t nw = ::write(fileFd_, buffer_.data(), writeLen);
                        (void)nw; // 消除警告，nw 可用于日志记录
                    } else {
                        fieldValue_.append(buffer_.data(), writeLen);
                    }
                    buffer_.erase(0, writeLen);
                }
                return; 
            }

            // 找到了 boundary，说明数据结束
            // 真正的有效数据在 pos 之前，还要减去前面的 \r\n (2字节)
            size_t dataLen = (pos >= 2) ? pos - 2 : 0; 
            
            if (isFile_ && fileFd_ >= 0) {
                ssize_t nw = ::write(fileFd_, buffer_.data(), dataLen);
                (void)nw; // 消除警告
                closeFile();
                result_.savedFiles.push_back(currentFilename_);
            } else {
                fieldValue_.append(buffer_.data(), dataLen);
                result_.fields[currentName_] = fieldValue_;
                fieldValue_.clear();
            }

            buffer_.erase(0, pos); // 回到 boundary 开头，进入下一轮循环
            state_ = State::SCAN_BOUNDARY;
        }
        
        if (state_ == State::DONE) return;
    }
}

void MultipartStreamParser::processHeader() {
    // 重置状态
    currentName_.clear();
    currentFilename_.clear();
    isFile_ = false;
    
    // 解析 Content-Disposition
    // 示例: Content-Disposition: form-data; name="file"; filename="a.mp4"
    auto cdPos = currentHeaders_.find("Content-Disposition:");
    if (cdPos == std::string::npos) return;

    // 提取 name
    auto namePos = currentHeaders_.find("name=\"", cdPos);
    if (namePos != std::string::npos) {
        namePos += 6;
        auto end = currentHeaders_.find("\"", namePos);
        currentName_ = currentHeaders_.substr(namePos, end - namePos);
    }

    // 提取 filename
    auto fnPos = currentHeaders_.find("filename=\"", cdPos);
    if (fnPos != std::string::npos) {
        fnPos += 10;
        auto end = currentHeaders_.find("\"", fnPos);
        currentFilename_ = currentHeaders_.substr(fnPos, end - fnPos);
        isFile_ = true;
        openFile();
    }
}

void MultipartStreamParser::openFile() {
    std::string path = uploadDir_ + currentFilename_;
    fileFd_ = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
}

void MultipartStreamParser::closeFile() {
    if (fileFd_ >= 0) {
        ::close(fileFd_);
        fileFd_ = -1;
    }
}

// 确保这个实现出现在 MultipartStreamParser.cpp 中
MultipartStreamParser::~MultipartStreamParser() {
    closeFile();
}

void MultipartStreamParser::finish() {
    if (state_ != State::DONE) {
        if (isFile_ && fileFd_ >= 0) {
            closeFile();
            result_.savedFiles.push_back(currentFilename_);
        }
        state_ = State::DONE;
    }
}