#include "EventDispatch.h"
#include "BaseSocket.h"

#include "Logging.h"

CEventDispatch *CEventDispatch::m_pEventDispatch = NULL;

CEventDispatch::CEventDispatch()
{
    running = false;
    m_epfd = epoll_create(1);
    if (m_epfd == -1)
    {
        LOG_ERROR << "epoll_create failed";
    }
}

CEventDispatch::~CEventDispatch()
{
    if (m_epfd >= 0)
        close(m_epfd);
}

void CEventDispatch::AddTimer(callback_t callback, void *user_data, uint64_t interval)
{
    auto it = m_timer_list.begin();
    for (; it != m_timer_list.end(); it++)
    {
        TimerItem *pItem = *it;
        if (pItem->callback == callback && pItem->user_data == user_data)
        {
            pItem->interval = interval;
            pItem->next_tick = GetTickCount() + interval; // 获取时间ms
            return;
        }
    }

    TimerItem *pItem = new TimerItem();
    pItem->callback = callback;
    pItem->user_data = user_data;
    pItem->interval = interval;
    pItem->next_tick = GetTickCount() + interval;
    m_timer_list.push_back(pItem);
}
void CEventDispatch::RemoveTimer(callback_t callback, void *user_data)
{
    auto it = m_timer_list.begin();
    for (; it != m_timer_list.end(); it++)
    {
        TimerItem *pItem = *it;
        if (pItem->callback == callback && pItem->user_data == user_data)
        {
            m_timer_list.erase(it);
            delete pItem;
            return;
        }
    }
}
void CEventDispatch::_CheckTimer()
{
    uint64_t curr_tick = GetTickCount();
    auto it = m_timer_list.begin();
    for (; it != m_timer_list.end();)
    {
        TimerItem *pItem = *it;
        it++; // 这里可能会删除删除元素，所以要先将迭代器++
        if (curr_tick >= pItem->next_tick)
        {
            pItem->next_tick += pItem->interval;
            pItem->callback(pItem->user_data, NETLIB_MSG_TIMER, 0, NULL);
        }
    }
}
void CEventDispatch::AddLoop(callback_t callback, void *user_data)
{
    TimerItem *pItem = new TimerItem();
    pItem->callback = callback;
    pItem->user_data = user_data;
    m_loop_list.push_back(pItem);
}
void CEventDispatch::_CheckLoop()
{
    // loop内的成员不会删除
    for (auto it = m_loop_list.begin(); it != m_loop_list.end(); it++)
    {
        TimerItem *pItem = *it;
        pItem->callback(pItem->user_data, NETLIB_MSG_LOOP, 0, NULL);
    }
}

CEventDispatch *CEventDispatch::Instance()
{
    if (m_pEventDispatch == NULL)
    {
        m_pEventDispatch = new CEventDispatch();
    }
    return m_pEventDispatch;
}

void CEventDispatch::AddEvent(SOCKET fd, uint8_t socket_event)
{
    struct epoll_event ev;
    uint32_t u = 0;
    if (socket_event & SOCKET_READ)
        u |= EPOLLIN;
    if (socket_event & SOCKET_WRITE)
        u |= EPOLLOUT;
    if (socket_event & SOCKET_EXCEP)
        u |= EPOLLPRI;
    ev.events = u;
    ev.data.fd = fd;
    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        if (errno == EEXIST && epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev) != 0)
        {
            LOG_ERROR << "epoll_ctl_mod failed,errno = " << errno;
        }
        else
        {
            LOG_ERROR << "epoll_ctl_add failed,errno = " << errno;
        }
    }
}
void CEventDispatch::RemoveEvent(SOCKET fd, uint8_t socket_event)
{
    if (epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, NULL) != 0)
    {
        LOG_ERROR << "epoll_ctl failed, errno= " << errno;
    }
}
void CEventDispatch::StartDispatch(uint32_t wait_timeout)
{
    struct epoll_event evs[1024];
    int nfds = 0;
    if (running)
        return;
    running = true;

    while (running)
    {
        nfds = epoll_wait(m_epfd, evs, 1024, wait_timeout);
        for (int i = 0; i < nfds; i++)
        {
            int ev_fd = evs[i].data.fd;
            CBaseSocket *pSocket = FindBaseSocket(ev_fd);
            if (!pSocket)
                continue;
#ifdef EPOLLRDHUP
            if (evs[i].events & EPOLLRDHUP)
            {
                pSocket->OnClose();
            }
#endif
            if (evs[i].events & EPOLLIN)
            {
                pSocket->OnRead();
            }
            if (evs[i].events & EPOLLOUT)
            {
                pSocket->OnWrite();
            }
            if (evs[i].events & (EPOLLPRI | EPOLLHUP | EPOLLERR))
            {
                pSocket->OnClose();
            }

            pSocket->ReleaseRef();
        }

        _CheckTimer();
        _CheckLoop();
    }
}
void CEventDispatch::StopDispatch()
{
    running = false;
}
