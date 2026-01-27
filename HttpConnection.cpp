#include "HttpConnection.h"
#include "TcpConnection.h"
#include "HttpRouter.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

constexpr size_t kMaxUriLength = 2048;

HttpConnection::HttpConnection(
    const std::shared_ptr<TcpConnection>& conn, HttpRouter* router)
    : conn_(conn),
    router_(router)
{
}

void HttpConnection::onMessage(Buffer* buf)
{
    while (true) {
        bool ok = parseRequest(buf);
        if (!ok) {
            break;  // 数据不完整 or 解析失败
        }

        if (state_ == ParseState::FINISH) {
            // ⭐⭐⭐ 上传请求：BODY 没收完，不能处理
            if (multipartParser_ && !uploadFinished_) {
                break;  // 等更多数据
            }

            handleRequest();

            // 为下一个请求重置
            state_ = ParseState::REQUEST_LINE;
            // request_ = HttpRequest();
            request_.reset();
            multipartParser_.reset();
            contentLength_ = 0;
            bodyReceived_ = 0;
            uploadFinished_ = false;
        } else {
            break;
        }
    }
}

bool HttpConnection::parseRequest(Buffer* buf)
{
    while (true) {

        // ===== 1️⃣ 请求行 / 头部 =====
        if (state_ == ParseState::REQUEST_LINE ||
            state_ == ParseState::HEADERS) {

            const char* crlf = std::search(
                buf->peek(),
                buf->peek() + buf->readableBytes(),
                "\r\n", "\r\n" + 2);

            if (crlf == buf->peek() + buf->readableBytes()) {
                return false; // 行还不完整
            }

            std::string line(buf->peek(), crlf);
            buf->retrieve(line.size() + 2);

            constexpr size_t kMaxRequestLine = 8192;
            constexpr size_t kMaxHeaderLine = 8192;
            constexpr size_t kMaxHeaders = 100;

            if (state_ == ParseState::REQUEST_LINE) {
                if (line.size() > kMaxRequestLine) {
                    request_.setError(HttpError::UriTooLong);
                    state_ = ParseState::FINISH;
                    return true;
                }

                if (!request_.parseRequestLine(line)) {
                    request_.setError(HttpError::BadRequest);
                    state_ = ParseState::FINISH;
                    return true;
                }

                // ⭐⭐ 新增：URI 长度限制（414）
                constexpr size_t kMaxUriLength = 2048;
                if (request_.path().size() > kMaxUriLength) {
                    request_.setError(HttpError::UriTooLong);
                    state_ = ParseState::FINISH;
                    return true;
                }

                state_ = ParseState::HEADERS;
            }
            else if (state_ == ParseState::HEADERS) {

                if (line.size() > kMaxHeaderLine) {
                    request_.setError(HttpError::HeaderTooLarge);
                    state_ = ParseState::FINISH;
                    return true;
                }

                // 空行：headers 结束
                if (line.empty()) {

                    // ===== ⭐⭐⭐ 100-continue 处理 =====
                    if (request_.hasHeader("Expect") &&
                        request_.getHeader("Expect") == "100-continue") {

                        // 如果前面已经判定有错误，就不要继续
                        if (request_.error() != HttpError::None) {
                            state_ = ParseState::FINISH;
                            return true;
                        }
                    }

                    if (request_.hasHeader("Content-Length")) {
                        try {
                            contentLength_ =
                                std::stoul(request_.getHeader("Content-Length"));
                        } catch (...) {
                            request_.setError(HttpError::BadRequest);
                            state_ = ParseState::FINISH;
                            return true;
                        }

                        // HEADERS 阶段直接拒绝超大 body
                        if (contentLength_ > HttpRequest::maxBodySize()) {
                            request_.setError(HttpError::PayloadTooLarge);
                            state_ = ParseState::FINISH;
                            return true;
                        }

                        // ⭐⭐⭐ 允许继续，回 100 Continue
                        if (request_.hasHeader("Expect") &&
                            request_.getHeader("Expect") == "100-continue") {

                            if (auto conn = conn_.lock()) {
                                conn->send("HTTP/1.1 100 Continue\r\n\r\n");
                            }
                        }

                        // ⭐⭐⭐ 在这里创建 multipart parser
                        if (request_.hasHeader("Content-Type")) {
                            std::string ct = request_.getHeader("Content-Type");
                            auto pos = ct.find("boundary=");
                            if (pos != std::string::npos &&
                                ct.find("multipart/form-data") != std::string::npos) {

                                std::string boundary = ct.substr(pos + 9);
                                multipartParser_.reset(
                                    new MultipartStreamParser(boundary)
                                );
                            }
                        }

                        state_ = ParseState::BODY;
                    } else {
                        state_ = ParseState::FINISH;
                    }
                    continue;
                }


                // header 数量限制
                if (request_.headerCount() >= kMaxHeaders) {
                    request_.setError(HttpError::HeaderTooLarge);
                    state_ = ParseState::FINISH;
                    return true;
                }

                auto pos = line.find(':');
                if (pos == std::string::npos) {
                    request_.setError(HttpError::BadRequest);
                    state_ = ParseState::FINISH;
                    return true;
                }

                request_.addHeader(
                    line.substr(0, pos),
                    line.substr(pos + 2));
            }
        }

        // ===== 2️⃣ BODY =====
        if (state_ == ParseState::BODY) {
            size_t readable = buf->readableBytes();
            size_t need = contentLength_ - bodyReceived_;
            size_t n = std::min(readable, need);

            // ⭐⭐⭐ 关键：流式写文件
            if (multipartParser_) {
                multipartParser_->onData(buf->peek(), n);
            }

            buf->retrieve(n);
            bodyReceived_ += n;

            if (bodyReceived_ == contentLength_) {
                if (multipartParser_) {
                    multipartParser_->finish();
                    uploadFinished_ = true;   // ⭐⭐⭐ 关键
                }
                state_ = ParseState::FINISH;
            } else {
                return false;
            }
        }


        // ===== 3️⃣ 完成 =====
        if (state_ == ParseState::FINISH) {
            return true;
        }
    }
}

void HttpConnection::handleRequest()
{
    if (multipartParser_) {
        // 取出解析结果，放入 request，传给 main.cpp 处理
        auto result = multipartParser_->getResult();
        request_.setSavedFiles(result.savedFiles);
        
        // 也可以把普通字段放入 parameters (如果你有实现参数解析的话)
        // for(auto& kv : result.fields) { request_.addParam(kv.first, kv.second); }
    }

    // ⭐⭐ HTTP 错误统一出口
    if (request_.error() != HttpError::None) {
        if (auto conn = conn_.lock()) {
            HttpResponse resp;
            resp.setKeepAlive(false);

            switch (request_.error()) {
            case HttpError::BadRequest:
                resp.setStatus(400, "Bad Request");
                break;
            case HttpError::UriTooLong:
                resp.setStatus(414, "URI Too Long");
                break;
            case HttpError::HeaderTooLarge:
                resp.setStatus(431, "Request Header Fields Too Large");
                break;
            case HttpError::PayloadTooLarge:
                resp.setStatus(413, "Payload Too Large");
                break;
            default:
                resp.setStatus(500, "Internal Server Error");
            }

            resp.setBody("");
            conn->send(resp.toString());
            conn->shutdown();
        }
        return;
    }

    // 2️⃣ 计算 keep-alive（保留）
    bool keepAlive = false;
    if (request_.version() == "HTTP/1.1") {
        keepAlive = true;
    }
    if (request_.version() == "HTTP/1.0" &&
        request_.hasHeader("Connection") &&
        request_.getHeader("Connection") == "keep-alive") {
        keepAlive = true;
    }
    if (request_.hasHeader("Connection") &&
        request_.getHeader("Connection") == "close") {
        keepAlive = false;
    }

    // 3️⃣ Router 驱动
    HttpResponse resp;
    resp.setKeepAlive(keepAlive);

    bool routed = router_ && router_->route(request_, resp);
    if (!routed) {
        resp.setStatus(404, "Not Found");
        resp.setBody("");
    }

    // 4️⃣ 发送
    if (auto conn = conn_.lock()) {

        conn->send(resp.toString());

        if (request_.methodString() != "HEAD" &&
            request_.methodString() != "POST") {

            if (resp.hasFile()) {
                conn->sendFile(
                    resp.fileFd(),
                    resp.fileOffset(), 
                    resp.fileSize()
                );
            }
        }

        // ⭐⭐⭐ sendfile 时禁止主动 shutdown
        if (resp.hasFile()) {
            if (keepAlive) {
                conn->refreshAlive();
            }
            return;
        }

        // POST：上传请求，不关连接
        if (request_.methodString() == "POST") {
            conn->refreshAlive();
            return;
        }

        if (keepAlive) {
            conn->refreshAlive();
        } else {
            conn->shutdown();
        }
    }
}


static bool readLine(Buffer* buf, std::string& line)
{
    const char* crlf = std::search(
        buf->peek(),
        buf->peek() + buf->readableBytes(),
        "\r\n", "\r\n" + 2);

    if (crlf == buf->peek() + buf->readableBytes()) {
        return false;
    }

    line.assign(buf->peek(), crlf);
    buf->retrieve(line.size() + 2);
    return true;
}

bool HttpConnection::parseRequestLine(Buffer* buf)
{
    std::string line;
    if (!readLine(buf, line)) return false;

    if (!request_.parseRequestLine(line)) {
        return false;
    }

    state_ = ParseState::HEADERS;
    return true;
}

bool HttpConnection::parseHeaders(Buffer* buf)
{
    std::string line;
    if (!readLine(buf, line)) return false;

    if (line.empty()) {
        state_ = ParseState::FINISH;
        return true;
    }

    auto pos = line.find(':');
    if (pos != std::string::npos) {
        request_.addHeader(
            line.substr(0, pos),
            line.substr(pos + 2));
    }
    return true;
}