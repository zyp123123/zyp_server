#pragma once
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include "HttpError.h"
#include <vector>
#include <map>

class HttpRequest {
public:
    enum class Method {
        GET,
        POST,
        HEAD,
        UNKNOWN   
    };

    HttpRequest() = default;

    // ===== 解析阶段 =====
    bool parseRequestLine(const std::string& line);

    // ===== setter =====
    void addHeader(const std::string& key, const std::string& value);
    void setMethod(const std::string& m);
    void reset();

    // ===== getter =====
    Method method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& version() const { return version_; }

    bool hasHeader(const std::string& key) const;
    const std::string& getHeader(const std::string& key) const;

    void setError(HttpError err) { error_ = err; }
    HttpError error() const { return error_; }

    void appendBody(const char* data, size_t len);
    const std::string& body() const { return body_; }\

    const std::string methodString() const;

    size_t headerCount() const;

    static constexpr size_t maxBodySize() { return kMaxBodySize; }

    bool hasRange() const;
    bool getRange(off_t& start, off_t& end) const;

    // --- 新增：保存上传成功的文件名列表 ---
    void setSavedFiles(const std::vector<std::string>& files) {
        savedFiles_ = files;
    }

    std::vector<std::string> savedFiles() const {
        return savedFiles_;
    }

private:
    Method method_{Method::UNKNOWN};
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    HttpError error_{HttpError::None};
    static constexpr size_t kMaxBodySize = 1024 * 1024 * 1024; // 1GB
    static constexpr size_t kMaxUriLength = 8192;

    std::vector<std::string> savedFiles_; // 新增
};
