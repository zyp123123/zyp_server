#include "EventLoop.h"
#include "TcpServer.h"
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include "log/Logger.h"
#include "log/AsyncLogger.h"
#include "MultipartParser.h"
#include <dirent.h>
#include <sys/types.h>

// 1. 全局指针，用于信号处理
EventLoop* g_loop = nullptr;

// 2. 优雅退出信号处理函数
void handleShutdown(int sig) {
    LOG_INFO << "接收到信号 (" << sig << ")，正在尝试关闭服务器...";
    if (g_loop) {
        g_loop->quit(); // 停止 EventLoop 循环
    }
}

// 3. 简单的配置加载类
struct Config {
    int port = 9999;
    int threadNum = 4;
    std::string staticPath = "./www/static";
    std::string logPath = "server.log";

    void load(const std::string& filename) {
        std::ifstream ifs(filename);
        if (!ifs.is_open()) return;
        std::string line;
        while (std::getline(ifs, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos || line[0] == '#') continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (key == "port") port = std::stoi(val);
            else if (key == "thread_num") threadNum = std::stoi(val);
            else if (key == "static_path") staticPath = val;
            else if (key == "log_path") logPath = val;
        }
    }
};

int main()
{
    // A. 加载配置
    Config conf;
    conf.load("server.conf");

    // B. 初始化异步日志
    logging::AsyncLogger asyncLogger(conf.logPath, 10 * 1024 * 1024, 7);
    asyncLogger.start();
    logging::Logger::setAsyncLogger(&asyncLogger);
    logging::Logger::setLogLevel(logging::LogLevel::INFO); // 生产环境通常用 INFO

    // C. 注册信号（优雅退出）
    g_loop = new EventLoop();
    signal(SIGINT, handleShutdown);  // 捕获 Ctrl+C
    signal(SIGTERM, handleShutdown); // 捕获 kill 命令
    signal(SIGPIPE, SIG_IGN);        // 忽略 Broken Pipe

    TcpServer server(g_loop, conf.port);

    //100000条日志测试
    // for (int i = 0; i < 100000; ++i) {
    //     LOG_INFO << "test log " << i;
    // }
    // LOG_INFO << "server starting...";

    //过滤测试
    // logging::Logger::setLogLevel(logging::LogLevel::WARN);
    // LOG_TRACE << "trace should NOT appear";
    // LOG_DEBUG << "debug should NOT appear";
    // LOG_INFO  << "info should NOT appear";
    // LOG_WARN  << "warn should appear";
    // LOG_ERROR << "error should appear";

    //FATAL测试
    // LOG_FATAL << "fatal test";
    // LOG_INFO << "should never print";

    signal(SIGPIPE, SIG_IGN);

    // EventLoop loop;
    // TcpServer server(&loop, 9999);

    // --- 路由 1: 首页 (展示文件列表) ---
    server.router().addRoute("GET", "/", [&conf](const HttpRequest& req, HttpResponse& resp) {
        DIR* dp = opendir(conf.staticPath.c_str());
        
        if (!dp) {
            LOG_ERROR << "Failed to open directory: " << conf.staticPath; // 记录严重错误
            resp.setStatus(404, "Not Found");
            resp.setBody("Storage directory missing. Please create ./www/static/");
            return;
        }
        LOG_INFO << "Rendering file list for client"; // 记录一次首页访问

        std::string html = "<html><head><meta charset='utf-8'><title>Private Cloud</title></head><body>";
        html += "<h2>📁 File List</h2><ul>";

        struct dirent* entry;
        while ((entry = readdir(dp)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue; 

            html += "<li>" + name + " &nbsp;&nbsp; " +
                    "<a href='/static/" + name + "'>[预览]</a> " +
                    "<a href='/static/" + name + "?download=1'>[下载]</a>" +
                    "</li>";
        }
        closedir(dp);

        html += "</ul><hr><h3>📤 Upload</h3>";
        html += "<form action='/upload' method='POST' enctype='multipart/form-data'>"
                "<input type='file' name='file' multiple><button type='submit'>Upload</button></form>";
        html += "</body></html>";

        resp.setStatus(200, "OK");
        resp.setHeader("Content-Type", "text/html; charset=utf-8");
        resp.setBody(html);
    });

    // --- 路由 2: 静态资源处理 (支持下载参数解析) ---
    server.router().addPrefixRoute("GET", "/static/", [&conf](const HttpRequest& req, HttpResponse& resp) {
        // 1. 获取原始路径
        std::string rawPath = req.path();
        
        // 2. ⭐ 手动解析 Query String (因为 HttpRequest 类目前没做解析)
        // 将 /static/test.mp4?download=1 拆分为真正的路径和参数
        bool shouldDownload = false;
        size_t questionMark = rawPath.find('?');
        std::string realRelativePath = rawPath;
        if (questionMark != std::string::npos) {
            std::string query = rawPath.substr(questionMark + 1);
            realRelativePath = rawPath.substr(0, questionMark);
            if (query.find("download=1") != std::string::npos) {
                shouldDownload = true;
            }
        }

        // 3. 安全检查
        if (realRelativePath.find("..") != std::string::npos) {
            LOG_WARN << "Security: Suspicious path attempt: " << req.path(); // 记录安全警告
            resp.setStatus(403, "Forbidden");
            return;
        }

        // 记录访问日志
        LOG_INFO << (shouldDownload ? "[DOWNLOAD] " : "[PREVIEW] ") << realRelativePath;

        std::string fullPath = conf.staticPath + realRelativePath.substr(7);
        int fd = ::open(fullPath.c_str(), O_RDONLY);
        if (fd < 0) {
            LOG_ERROR << "File not found: " << fullPath; // 记录路径错误
            resp.setStatus(404, "Not Found");
            return;
        }

        struct stat st;
        if (::fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
            ::close(fd);
            resp.setStatus(404, "Not Found");
            return;
        }

        // 4. 设置 Content-Type
        if (fullPath.find(".txt") != std::string::npos) resp.setHeader("Content-Type", "text/plain");
        else if (fullPath.find(".mp4") != std::string::npos) resp.setHeader("Content-Type", "video/mp4");
        else resp.setHeader("Content-Type", "application/octet-stream");

        // 5. 设置下载头
        if (shouldDownload) {
            std::string filename = fullPath.substr(fullPath.find_last_of('/') + 1);
            resp.setHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        }

        resp.setHeader("Accept-Ranges", "bytes");

        // 6. Range 处理逻辑 (直接复用你之前的)
        off_t start = 0, end = st.st_size - 1;
        if (req.getRange(start, end)) {
            if (end == -1) end = st.st_size - 1;
            off_t len = end - start + 1;
            resp.setStatus(206, "Partial Content");
            resp.setHeader("Content-Range", "bytes " + std::to_string(start) + "-" + 
                          std::to_string(end) + "/" + std::to_string(st.st_size));
            resp.setHeader("Content-Length", std::to_string(len));
            resp.setFile(fd, start, len);
        } else {
            resp.setStatus(200, "OK");
            resp.setHeader("Content-Length", std::to_string(st.st_size));
            resp.setFile(fd, 0, st.st_size);
        }
    });

    // --- 路由 3: 上传处理 ---
    server.router().addRoute("POST", "/upload", [](const HttpRequest& req, HttpResponse& resp) {
        auto files = req.savedFiles();
        if (files.empty()) {
            LOG_WARN << "Upload request received but no files saved.";
        } else {
            for (const auto& f : files) {
                LOG_INFO << "Successfully uploaded file: " << f;
            }
        }
        resp.setStatus(200, "OK");
        resp.setHeader("Content-Type", "text/html; charset=utf-8");
        std::string msg = "<h2>Upload Success</h2><ul>";
        for (const auto& f : files) msg += "<li>" + f + "</li>";
        msg += "</ul><a href='/'>Back to Home</a>";
        resp.setBody(msg);
    });

    LOG_INFO << "Server starting at port " << conf.port << "...";
    server.start();
    g_loop->loop();

    LOG_INFO << "Server stopped gracefully.";
    delete g_loop;
    return 0;

    // server.start();
    // loop.loop();
}
