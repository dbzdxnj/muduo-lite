#include "TcpServer.h"
#include "logger.h"

#include <string.h>

static EventLoop* checkLoopNotNull(EventLoop* loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainloop is null \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop* loop, 
            const InetAddress &listenAddr,
            const std::string nameArg, 
            Option option)
            : loop_(checkLoopNotNull(loop)), 
              ipPort_(listenAddr.toIpPort()),
              name_(nameArg),
              acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
              threadPool_(new EventLoopThreadPool(loop, name_)),
              connectionCallback_(),
              messageCallback_(),
              nextConnId_(1),
              started_(0)
{
    // 当有新用户连接时，会执行TcpConnection回调
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
            std::placeholders::_1, std::placeholders::_2));
}

//析构函数
TcpServer::~TcpServer()
{
    for (auto& item : connections_)
    {
        //这个局部的shared_ptr智能指针对象出右括号，可以自动释放资源
        TcpConnectionPtr conn(item.second); 
        item.second.reset();

        // 销毁连接
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn)
        );
    }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

//开启服务器监听
void TcpServer::start()
{
    if (started_++ == 0)   //防止一个TcpServer对象被start多次
    {
        threadPool_->start(threadInitCallback_);    //启动底层线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

// 有一个新的客户端连接，acceptor会执行这个回调操作
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    EventLoop* ioLoop = threadPool_->getNextLoop();
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
        name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    bzero(&local, sizeof local);
    socklen_t addrlen = static_cast<socklen_t>(sizeof local);
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("socket::getLocalAddr");
    }
    InetAddress localAddr(local);

    // 根据连接成功的sockefd，创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(
                                ioLoop, 
                                connName, 
                                sockfd,
                                localAddr,
                                peerAddr
                            ));
    connections_[connName] = conn;
    // 下面回调都是用户设置给TcpServer => TcpConnection => Channel => poller => notify channel
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调 conn => shutDown()
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    
    //直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn)
    );
}
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s \n",
        name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn)
    );
}
