#include "HttpRouter.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

bool HttpRouter::route(const HttpRequest& req, HttpResponse& resp) const
{
    std::string method = req.methodString();

    // ⭐ HEAD fallback to GET
    if (method == "HEAD") {
        method = "GET";
    }

    // 1️⃣ exact
    auto key = method + ":" + req.path();
    auto it = exactRoutes_.find(key);
    if (it != exactRoutes_.end()) {
        it->second(req, resp);
        return true;
    }

    // 2️⃣ prefix
    for (const auto& r : prefixRoutes_) {
        if (r.method == method &&
            req.path().rfind(r.prefix, 0) == 0) {
            r.handler(req, resp);
            return true;
        }
    }

    return false;
}

