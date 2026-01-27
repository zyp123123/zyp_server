#pragma once
#include <string>
#include <unordered_map>
#include <sys/types.h>

class HttpResponse {
public:
    void setStatus(int code, const std::string& msg);
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);

    std::string toString() const;

    void setKeepAlive(bool on) { keepAlive_ = on; }
    bool keepAlive() const { return keepAlive_; }

    // ⭐ 修改：统一接口，只保留一套
    void setFile(int fd, off_t offset, off_t size);
    
    // 兼容之前的 setSendFile 调用（可选，或者你把业务代码改成调 setFile）
    void setSendFile(int fd, off_t size) {
        setFile(fd, 0, size);
    }

    bool hasFile() const { return fileFd_ >= 0; }
    int  fileFd() const { return fileFd_; }
    off_t fileOffset() const { return fileOffset_; }
    off_t fileSize() const { return fileSize_; }

private:
    int statusCode_{200};
    std::string statusMsg_{"OK"};
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    bool keepAlive_{false};
    
    // ⭐ 修改：只保留一套变量
    int   fileFd_{-1};
    off_t fileOffset_{0};
    off_t fileSize_{0};
};