#pragma once

#include "ostype.h"
#include "util.h"
#include "Lock.h"

enum
{
    SOCKET_READ = 0x1,
    SOCKET_WRITE = 0x2,
    SOCKET_EXCEP = 0x4,
    SOCKET_ALL = 0x7,
};

class CEventDispatch
{
public:
    virtual ~CEventDispatch();

    void AddEvent(SOCKET fd, uint8_t socket_event);
    void RemoveEvent(SOCKET fd, uint8_t socket_event);

    void AddTimer(callback_t callback, void *user_data, uint64_t interval);
    void RemoveTimer(callback_t callback, void *user_data);

    void AddLoop(callback_t callback, void *user_data);

    void StartDispatch(uint32_t wait_timeout = 100);
    void StopDispatch();

    bool isRunning() { return running; }

    static CEventDispatch *Instance();

protected:
    CEventDispatch();

private:
    void _CheckTimer();
    void _CheckLoop();
    typedef struct
    {
        callback_t callback;
        void *user_data;
        uint64_t interval;
        uint64_t next_tick;
    } TimerItem;

private:
    int m_epfd;

    CLock m_lock;
    // loop每次事件循环都跑一遍，timer是定时任务
    std::list<TimerItem *> m_timer_list;
    std::list<TimerItem *> m_loop_list;

    static CEventDispatch *m_pEventDispatch;

    bool running;
};