#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <vector>

constexpr int PORT = 9999;
constexpr int MAX_EVENTS = 1024;

int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int createListenFd() {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, SOMAXCONN) < 0) {
        perror("listen");
        exit(1);
    }

    setNonBlocking(listenfd);
    return listenfd;
}

int main() {
    int listenfd = createListenFd();

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    std::vector<epoll_event> events(MAX_EVENTS);

    std::cout << "Echo server running on port " << PORT << std::endl;

    while (true) {
        int nfds = epoll_wait(epfd, events.data(), MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            // 1️⃣ 新连接
            if (fd == listenfd) {
                while (true) {
                    sockaddr_in client{};
                    socklen_t len = sizeof(client);
                    int connfd = accept(listenfd, (sockaddr*)&client, &len);
                    if (connfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }

                    setNonBlocking(connfd);

                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = connfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &cev);

                    std::cout << "New connection fd=" << connfd << std::endl;
                }
            }
            // 2️⃣ 已连接 socket 可读
            else {
                char buf[4096];
                while (true) {
                    ssize_t n = read(fd, buf, sizeof(buf));
                    if (n > 0) {
                        write(fd, buf, n);  // Echo
                    } else if (n == 0) {
                        std::cout << "Client disconnected fd=" << fd << std::endl;
                        close(fd);
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("read");
                        close(fd);
                        break;
                    }
                }
            }
        }
    }

    close(listenfd);
    close(epfd);
    return 0;
}
