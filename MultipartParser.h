#pragma once
#include <string>
#include <vector>

struct MultipartFile {
    std::string name;        // 表单字段名
    std::string filename;    // 文件名
    std::string contentType; // Content-Type
    std::string data;        // 文件内容
};

class MultipartParser {
public:
    static bool parseAll(const std::string& body,
                         const std::string& boundary,
                         std::vector<MultipartFile>& files);
};
