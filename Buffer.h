#pragma once

#include "noncopyable.h"

#include <vector>
#include <aio.h>
#include <string>
#include <algorithm>

class Buffer : noncopyable
{
public:
    static const size_t kCheapPreapend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPreapend + initialSize),
          readerIndex_(kCheapPreapend),
          writerIndex_(kCheapPreapend)
        {}

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const{ return readerIndex_; }
    const char* peek() const { return begin() + readerIndex_; } //返回缓冲区中可读数据的起始地址
    char* beginWrite() {return begin() + writerIndex_; }
    const char* beginWrite() const {return begin() + writerIndex_; }

    // onMessage string -> Buffer
    void retrieve(size_t len) 
    {
        if (len < readableBytes())
        {
            readerIndex_ += len; //应用只读取了缓冲区的一部分，就是len，还剩下一部分
        }
        else
        {
            retrieveAll();
        }
    }
    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPreapend;
    }
    //把onMessage函数上报的Buffer数据，转成string类型的数据返回
    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);  //上面一句把缓冲区中可读的数据，已经读取出来，这里进行复位操作；
        return result;
    }
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    // buffer_.size - writerIndex       len
    void ensureWriteableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
    }

    //扩容函数
    void makeSpace(size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPreapend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                begin() + writerIndex_, 
                begin() + kCheapPreapend);
            readerIndex_ = kCheapPreapend;
            writerIndex_ = readable + readerIndex_;
        }
    }

    //向缓冲区添加数据 [data, data + len]
    void append(const char* data, size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    // 从fd上读数据
    ssize_t readFd(int fd, int *saveErrno);
    // 通过fd发送数据
    ssize_t writeFd(int fd, int *saveErrno);

private:
    char* begin() { return &*buffer_.begin(); } //vector底层数组首元素地址
    const char* begin() const { return &*buffer_.begin(); } //常对象使用
    
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};