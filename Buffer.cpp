#include "Buffer.h"
#include <unistd.h>
#include <cstring>

Buffer::Buffer(size_t initialSize)
    : buffer_(initialSize),
      readIndex_(0),
      writeIndex_(0) {}

/*返回能够安全读取到的字节数*/
size_t Buffer::readableBytes() const {  
    return writeIndex_ - readIndex_;
}

/*返回还能往buffer中写入的字节数*/
size_t Buffer::writableBytes() const {  
    return buffer_.size() - writeIndex_;
}

/**
 * 返回当前可读数据的起始地址（不移动读指针）
 * buffer_.data()表示vector 内部连续内存的起始地址（第 0 个元素）,
 * 它不是“已读数据”，而是整个数组的起点
*/
const char* Buffer::peek() const {
    /*指向“第一个还没被消费的数据”,就是说readIndex_指向了有效数据的开始位置*/
    return buffer_.data() + readIndex_;     
}

/*消费 len 字节的可读数据，通过推进读指针来标记数据已被使用，
    在上层http读取数据后这里将其标记为已读数据，如果没有可读空间就覆盖之前的数据*/
void Buffer::retrieve(size_t len) {     
    if (len < readableBytes()) {    /*如果只消费一部分：移动 readIndex_,跳过字节*/
        readIndex_ += len;
    } else {    /*如果消费完：直接 reset*/
        retrieveAll();
    }
}

/*重置内存，复用内存，避免频繁分配*/
void Buffer::retrieveAll() {    
    /*并不是释放了已读数据，而是因为有效数据没了，剩下的数据不需要保护，
        所以就将边界重新设置为0，下次数据就直接覆盖了，里面其实还有数据*/
    readIndex_ = writeIndex_ = 0;   
}

//len长度字节转字符串
std::string Buffer::retrieveAsString(size_t len) {  
    /*从Buffer中拷贝len个字节转化为string类型*/
    std::string result(peek(), len);    
    /*告诉buffer这些字节已经用过了*/
    retrieve(len);  
    return result;
}

/*把当前所有可读数据，一次性取走，全部转换为string类型*/
std::string Buffer::retrieveAllAsString() { 
    return retrieveAsString(readableBytes());
}

/*确保 Buffer 中有足够的可写空间*/
void Buffer::ensureWritable(size_t len) {   
    /* 若当前可写空间不足，则直接扩容（不做数据前移压缩）*/
    if (writableBytes() < len) {    
        buffer_.resize(writeIndex_ + len);
    }
}

/**
 * 向buffer中写入数据
 * const char* data 写入的数据
 * size_t len 数据长度
*/
void Buffer::append(const char* data, size_t len) {
    /*保证有空间写入*/
    ensureWritable(len);    
    /*从内存的起始地址+writeIndex_的位置推进写入len长度的data数据*/
    std::memcpy(buffer_.data() + writeIndex_, data, len); 
    /*写入内容后更新writeIndex_位置*/  
    writeIndex_ += len;     
}

/*把fd数据写入Buffer*/
ssize_t Buffer::readFd(int fd) {    
    /**
     * 为什么不用直接读到 buffer_？
        因为：
        不知道 fd 上有多少数据
        buffer_ 当前 writable 可能不够
    */
   /*定义一个大栈数组*/
    char extra[65536];  
    /*一次read把内核缓冲区尽量读空，避免多次epoll触发，栈上分配：快、自动释放*/
    ssize_t n = read(fd, extra, sizeof(extra)); 
    if (n > 0) {    /*知道有n大小的数据后就可以用append读取数据到buffer中*/
        append(extra, n);
    }
    return n;
}

/*把 Buffer 里“可读数据”，写到 fd*/
ssize_t Buffer::writeFd(int fd) {  
    ssize_t n = write(fd, peek(), readableBytes());
    if (n > 0) {
        retrieve(n);
    }
    return n;
}
