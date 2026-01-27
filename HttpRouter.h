#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HttpRequest.h"   // ⭐ 必须 include
#include "HttpResponse.h"

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpRouter {
public:
    void addRoute(const std::string& method,
                  const std::string& path,
                  HttpHandler handler)
    {
        exactRoutes_[method + ":" + path] = std::move(handler);
    }

    void addPrefixRoute(const std::string& method,
                        const std::string& prefix,
                        HttpHandler handler)
    {
        prefixRoutes_.push_back({method, prefix, std::move(handler)});
    }

    bool route(const HttpRequest& req, HttpResponse& resp) const;

private:
    struct PrefixRoute {
        std::string method;
        std::string prefix;
        HttpHandler handler;
    };

    std::unordered_map<std::string, HttpHandler> exactRoutes_;
    std::vector<PrefixRoute> prefixRoutes_;
};
