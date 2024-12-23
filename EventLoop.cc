#include "EventLoop.h"
#include "logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

//防止一个线程创建多个EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

//默认IO 复用接口的超时时间
const int kPollerTime = 10000;

int createEventfd()
{
    int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("event error: %d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    :looping_(false)
    , quit_(false)
    , CallingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , CurrenActiveChannels_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another eventloop %p exists in this thread %d \n", this, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }
    
    //设置wakeupfd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    //每一个EventLoop都将监听wakeupChannel的EPOLLIN事件
    wakeupChannel_->enabeReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop :: handleRead() reads %ld instead of 8 bytes \n", n);
    }
}

void EventLoop::loop() 
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd，1. client Fd     2. wakeupFd
        pollReturnTime_ = poller_->poll(kPollerTime, &activeChannels_);
        
        for (Channel *channel : activeChannels_)
        {
            //Poller监听哪些channel发生了事件，上报给EventLoop，然后EventLoop来处理这些事件
            channel->handleEvent(pollReturnTime_);
        }
        //执行当前EventLoop循环所需要的回调操作
        // mainloop事先注册一个回调cb（需要subloop来执行）， wakeup subloop后，执行下面的回调方法，执行mainloop注册的回调方法

        doPendingFunctors();
        
    }
    looping_ = false;
    LOG_INFO("EventLoop %p is stop looping...\n", this);
    
}

//退出循环事件 1.loop在自己的线程中调用自己 2.在非loop的线程中调用loop的quit
void EventLoop::quit()
{
    quit_ = true;
    //如果是在其他的线程中，调用的quit
    if (!isInLoopThread())
    {
        wakeup();
    }
    
}

//在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        cb();
    }
    else //在非当前loop线程中执行，需要先唤醒loop所在线程，执行cb   
    {
        queueInLoop(cb);
    }
}
//把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
    //唤醒相应的需要执行上述回调操作的线程  
    //CallingPendingFunctors_ 是指当前loop正在进行回调操作，而此时给当前EventLoop增加了新的回调；
    if (!isInLoopThread() || CallingPendingFunctors_ )
    {
        wakeup();
    }   
}

//调用poller的方法
void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}
void EventLoop::hasChannel(Channel* channel)
{
    poller_->hasChannel(channel);
}

//唤醒loop所在的线程
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop :: wakeup() writes %lu instead of 8 bytes \n", n);
    }
}

//执行回调
void EventLoop::doPendingFunctors() 
{
    std::vector<Functor> functors;
    CallingPendingFunctors_ = true;
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor &functor : functors)
    {
        functor(); //执行当前loop所需执行的回调操作
    }
    CallingPendingFunctors_ = false;
}


