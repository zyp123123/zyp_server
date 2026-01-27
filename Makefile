CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -g
LDFLAGS = -pthread

TARGET = reactor_server

SRCS = \
    main.cpp \
    EventLoop.cpp \
    Epoll.cpp \
    Buffer.cpp \
    Channel.cpp \
    EventLoopThread.cpp \
    EventLoopThreadPool.cpp \
    TcpServer.cpp \
    TcpConnection.cpp \
    HttpConnection.cpp \
    HttpRequest.cpp \
    HttpResponse.cpp \
    TimerQueue.cpp \
	HttpRouter.cpp \
    log/Logger.cpp \
    log/AsyncLogger.cpp \
    MultipartParser.cpp \
    MultipartStreamParser.cpp

OBJS = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
