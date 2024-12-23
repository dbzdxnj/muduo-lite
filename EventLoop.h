#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;
//事件循环类 主要包括 channel 和 poller(epoll的抽象)
class EventLoop
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    //开启事件循环
    void loop();
    //终止事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    //在当前loop中执行cb
    void runInLoop(Functor cb);
    //把cb放入队列中，唤醒loop所在的线程，执行cb
    void queueInLoop(Functor cb);

    //唤醒loop所在的线程
    void wakeup();

    //EventLoop的方法 => 调用Poller的方法
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    void hasChannel(Channel* channel);

    //判断EventLoop的对象是否在自己的线程里面
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
    void handleRead();  //唤醒wakeup
    void doPendingFunctors();   //执行回调

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_; //原子操作，底层通过CAS实现
    std::atomic_bool quit_; //标识退出循环

    const pid_t threadId_;  //记录当前loop所在线程的Id

    Timestamp pollReturnTime_;   //poll返回发生事件的时间点
    std::unique_ptr<Poller> poller_;    

    int wakeupFd_; //当mainLoop获取一个新用户的channel后，通过轮询算法选择一个subloop，通过该成员唤醒subloop来执行工作
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;
    Channel *CurrenActiveChannels_;

    std::atomic_bool CallingPendingFunctors_;    //标识当前loop是否有需要回调的操作
    std::vector<Functor> pendingFunctors_;  //存储loop所需要执行的所有回调操作
    std::mutex mutex_;  //互斥锁，保护上述vector的线程安全操作
};

