#pragma once
#include <vector>
#include <string>

class Buffer {
public:
    Buffer(size_t initialSize = 1024);

    //返回能够安全读取到的字节数
    size_t readableBytes() const;
    //返回还能往buffer中写入的字节数
    size_t writableBytes() const;

    //返回当前可读数据的起始地址
    const char* peek() const;

    //消费len字节的可读数据，通过推进读指针来标记数据已被使用
    void retrieve(size_t len);
    //内存复用
    void retrieveAll(); 

    //部分字节转字符串
    std::string retrieveAsString(size_t len);  
    //全部字节转字符串 
    std::string retrieveAllAsString();  

    //控制将数据写入buffer中
    void append(const char* data, size_t len);  

    //将数据从fd写入buffer
    ssize_t readFd(int fd); 
    //将数据从buffer写入fd
    ssize_t writeFd(int fd);    

private:
    std::vector<char> buffer_;
    //当前可读数据的起始下标
    size_t readIndex_;  
    //当前可写空间的起始下标（也是已写数据的结束位置）
    size_t writeIndex_; 
    //确保当数据超出buffer时能够写入
    void ensureWritable(size_t len);    
};
