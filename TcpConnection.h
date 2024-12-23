#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

//TcpServer => Acceptor => 有一个新用户连接， 通过accept（）拿到connfd
// => TcpConnection 设置回调 => Channel => poller =>channel的回调操作

class TcpConnection : noncopyable,
         public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                const std::string &name,
                int sockfd,
                const InetAddress &localAddr,
                const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected () const {return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisconnected; }

    //发送数据
    void send(const std::string& buf);
    //关闭连接
    void shutdown();

    //回调函数
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highwaterMark) 
        { highWaterMarkCallback_ = cb; highwaterMark_ = highwaterMark;}
    void setConnectionCallback(const ConnectionCallback &cb) 
        { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) 
        { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) 
        { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback &cb) 
        { closeCallback_ = cb; }
    
    //连接建立
    void connectEstablished();
    //连接销毁
    void connectDestroyed();

private:
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting };
    void setState(StateE s) { state_ = s; }
    
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInloop(const void *message, size_t len);
    void shutdownInLoop();


    EventLoop *loop_;    //绝对不是baseLoop， 因为TcpConnection都是在subloop中
    const std::string name_;    
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_; //有新连接时的回调
    MessageCallback messageCallback_;       //有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;   //信息发送完成后的回调
    CloseCallback closeCallback_;   //关闭连接时的回调
    HighWaterMarkCallback highWaterMarkCallback_;   //发送速率过高的回调

    size_t highwaterMark_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};