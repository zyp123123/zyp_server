#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

struct MultipartResult {
    std::unordered_map<std::string, std::string> fields; // 普通表单字段
    std::vector<std::string> savedFiles;                 // 已保存的文件名
};

class MultipartStreamParser {
public:
    MultipartStreamParser(const std::string& boundary, const std::string& uploadDir = "./upload/")
        : boundary_("--" + boundary), uploadDir_(uploadDir) {}

    ~MultipartStreamParser(); 

    // 每次收到数据调用
    void onData(const char* data, size_t len);

    bool isDone() const { return state_ == State::DONE; }
    
    const MultipartResult& getResult() const { return result_; }

    void finish();

private:
    enum class State { SCAN_BOUNDARY, READ_HEADERS, READ_DATA, DONE };
    State state_{State::SCAN_BOUNDARY};

    std::string boundary_;
    std::string uploadDir_;
    std::string buffer_;
    
    // 当前正在处理的 Part 信息
    std::string currentHeaders_;
    std::string currentName_;     // 表单字段名
    std::string currentFilename_; // 文件名(如果是文件)
    bool isFile_{false};          // 当前部分是否是文件
    int fileFd_{-1};              // 文件句柄
    std::string fieldValue_;      // 普通字段的值缓存

    MultipartResult result_;

    void processHeader();
    void openFile();
    void closeFile();
};