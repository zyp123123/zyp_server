#pragma once
#include <memory>

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Buffer.h"
#include "MultipartStreamParser.h"

class TcpConnection;
class HttpRouter;

class HttpConnection {
public:
    explicit HttpConnection(const std::shared_ptr<TcpConnection>& conn,
                   HttpRouter* router);

    // TcpConnection 收到数据后会调用
    void onMessage(Buffer* input);

private:
    bool parseRequestLine(Buffer* buf);
    bool parseHeaders(Buffer* buf);

    bool parseRequest(Buffer* buf);
    void handleRequest();

    std::weak_ptr<TcpConnection> conn_;
    HttpRequest request_;

    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH
    };

    size_t contentLength_{0};
    size_t bodyReceived_{0};

    ParseState state_{ParseState::REQUEST_LINE};

    static constexpr size_t kMaxBodySize = 1 * 1024 * 1024; // 1MB

    HttpRouter* router_;

    std::unique_ptr<MultipartStreamParser> multipartParser_;

    bool uploadFinished_{false};
};
