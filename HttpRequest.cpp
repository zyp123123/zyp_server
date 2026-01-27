#include "HttpRequest.h"
#include <sstream>
#include <algorithm>  // std::transform
#include <cctype>     // std::tolower

bool HttpRequest::parseRequestLine(const std::string& line)
{
    std::istringstream iss(line);
    std::string method;

    iss >> method >> path_ >> version_;
    setMethod(method);

    // ⭐ 414 URI Too Long
    if (path_.size() > kMaxUriLength) {
        error_ = HttpError::UriTooLong;
        return false;
    }

    return !path_.empty() &&
       !version_.empty();
}

void HttpRequest::setMethod(const std::string& m)
{
    if (m == "GET") {
        method_ = Method::GET;
    } else if (m == "POST") {
        method_ = Method::POST;
    } else if (m == "HEAD") {
        method_ = Method::HEAD;
    } else {
        method_ = Method::UNKNOWN;
    }
}

void HttpRequest::addHeader(const std::string& key,
                            const std::string& value)
{
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(),
               lowerKey.begin(),
               [](unsigned char c) { return std::tolower(c); });

    std::string v = value;
    while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) {
        v.erase(v.begin());
    }
    headers_[lowerKey] = v;
}

bool HttpRequest::hasHeader(const std::string& key) const
{
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(),
               lowerKey.begin(),
               [](unsigned char c) { return std::tolower(c); });

    return headers_.find(lowerKey) != headers_.end();
}

const std::string& HttpRequest::getHeader(const std::string& key) const
{
    static const std::string empty;
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(),
               lowerKey.begin(),
               [](unsigned char c) { return std::tolower(c); });


    auto it = headers_.find(lowerKey);
    return it != headers_.end() ? it->second : empty;
}

void HttpRequest::appendBody(const char* data, size_t len)
{
    if (body_.size() + len > kMaxBodySize) {
        error_ = HttpError::PayloadTooLarge;
        return;
    }
    body_.append(data, len);
}

void HttpRequest::reset()
{
    method_ = Method::UNKNOWN;
    path_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    error_ = HttpError::None;
}

const std::string HttpRequest::methodString() const
{
    switch (method_) {
    case Method::GET:
        return "GET";
    case Method::POST:
        return "POST";
    case Method::HEAD:
        return "HEAD";
    default:
        return "UNKNOWN";
    }
}

size_t HttpRequest::headerCount() const {
    return headers_.size();
}

bool HttpRequest::hasRange() const {
    return hasHeader("Range");
}

bool HttpRequest::getRange(off_t& start, off_t& end) const {
    auto range = getHeader("Range");   // "bytes=100-200"

    if (range.compare(0, 6, "bytes=") != 0)
        return false;

    auto dash = range.find('-');
    if (dash == std::string::npos)
        return false;

    std::string s1 = range.substr(6, dash - 6);
    std::string s2 = range.substr(dash + 1);

    start = std::stoll(s1);
    if (s2.empty()) {
        end = -1;     // 到文件末尾
    } else {
        end = std::stoll(s2);
    }
    return true;
}
