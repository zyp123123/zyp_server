#include "MultipartParser.h"

bool MultipartParser::parseAll(const std::string& body,
                               const std::string& boundary,
                               std::vector<MultipartFile>& files)
{
    size_t pos = 0;

    while (true) {
        // 找 boundary
        size_t start = body.find(boundary, pos);
        if (start == std::string::npos)
            break;

        start += boundary.size();
        if (body.compare(start, 2, "--") == 0)
            break; // 结束

        // 跳过 CRLF
        if (body.compare(start, 2, "\r\n") == 0)
            start += 2;

        // 头部结束
        size_t headerEnd = body.find("\r\n\r\n", start);
        if (headerEnd == std::string::npos)
            return false;

        std::string headers = body.substr(start, headerEnd - start);

        MultipartFile file;

        // Content-Disposition
        auto cdPos = headers.find("Content-Disposition:");
        if (cdPos != std::string::npos) {
            auto fnPos = headers.find("filename=\"", cdPos);
            if (fnPos != std::string::npos) {
                fnPos += 10;
                auto end = headers.find("\"", fnPos);
                file.filename = headers.substr(fnPos, end - fnPos);
            }
        }

        // Content-Type
        auto ctPos = headers.find("Content-Type:");
        if (ctPos != std::string::npos) {
            auto end = headers.find("\r\n", ctPos);
            file.contentType =
                headers.substr(ctPos + 13, end - (ctPos + 13));
        }

        // 数据区
        size_t dataStart = headerEnd + 4;
        size_t dataEnd = body.find(boundary, dataStart);
        if (dataEnd == std::string::npos)
            return false;

        file.data = body.substr(dataStart, dataEnd - dataStart - 2); // 去掉 \r\n

        files.push_back(std::move(file));

        pos = dataEnd;
    }

    return !files.empty();
}
