#include "EpollPoller.h"
#include "logger.h"
#include "Channel.h"

#include <iostream>
#include <string.h>

const int kNew = -1;
const int kAdded = 1;
const int kdeleted = 2;

EpollPoller::EpollPoller(EventLoop *loop)
    :Poller(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
    events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
    
}

EpollPoller::~EpollPoller()
{
    ::close(epollfd_);
}

//channel update romove => EventLoop update romove => poller update romove
void EpollPoller::updateChannel(Channel *channel)  
{
    const int index = channel->index();

    LOG_INFO("func = %s> fd = %d events = %d index = %d \n", __FUNCTION__, channel->fd(), channel->events(), index);
    
    if (index == kNew || index == kdeleted)
    {
        if (index == kNew)
        {       
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else    //channel已经在poller上注册过了
    {
        int fd = channel->fd();
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kdeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }   
    }
}

void EpollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    int index = channel->index();
    channels_.erase(fd);

    LOG_INFO("func = %s> fd = %d \n", __FUNCTION__, fd);

    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 填写活跃的连接
void EpollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        // LOG_INFO("Processing event %d, fd = %d", i, events_[i].data.fd);
        // LOG_INFO("Channel ptr = %p", events_[i].data.ptr);
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

// 更新channel通道
void EpollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    bzero(&event, sizeof event);

    event.events = channel->events();
    event.data.fd = channel->fd();
    event.data.ptr = channel;

    int fd = channel->fd();
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error : %d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error : %d\n", errno);
        }
    }
}

//epoll_wait
Timestamp EpollPoller::poll(int timeoutMs, ChannelList *activeChannels) 
{
    //实际上用LOG_DEBUG更合适
    
    LOG_INFO("func = %s, total counts = %zu \n", __FUNCTION__, channels_.size());
    
    int numEvents = epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;

    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happend \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);

        //扩容操作
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("function = %s, time out !\n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EpollPoller::poll() ERROR, errno = %d (%s)", errno, strerror(errno));
        }
        
    }
    return now;
}