#include "HttpResponse.h"
#include <sstream>

void HttpResponse::setStatus(int code, const std::string& msg)
{
    statusCode_ = code;
    statusMsg_ = msg;
}

void HttpResponse::setHeader(const std::string& key,
                             const std::string& value)
{
    headers_[key] = value;
}

void HttpResponse::setBody(const std::string& body)
{
    body_ = body;
}

std::string HttpResponse::toString() const
{
    std::ostringstream oss;

    // 1️⃣ 状态行
    oss << "HTTP/1.1 "
        << statusCode_ << " "
        << statusMsg_ << "\r\n";

    // 2️⃣ Connection
    if (keepAlive_) {
        oss << "Connection: keep-alive\r\n";
    } else {
        oss << "Connection: close\r\n";
    }

    // 3️⃣ Headers
    for (const auto& h : headers_) {
        oss << h.first << ": " << h.second << "\r\n";
    }

    // 4️⃣ Content-Length（优化逻辑） ⭐⭐
    // 检查 headers_ 中是否已经手动设置了 Content-Length
    if (!hasFile() &&
        headers_.find("Content-Length") == headers_.end()) {
        oss << "Content-Length: " << body_.size() << "\r\n";
    }

    // 5️⃣ Header 结束
    oss << "\r\n";

    // 6️⃣ Body
    oss << body_;

    return oss.str();
}

void HttpResponse::setFile(int fd, off_t offset, off_t size)
{
    fileFd_ = fd;
    fileOffset_ = offset;
    fileSize_ = size;
}
