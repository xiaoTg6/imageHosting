#include "ThreadPool.h"

ThreadPool::ThreadPool() : threadNum_(1), bTerminate_(false) {}
ThreadPool::~ThreadPool()
{
    stop();
}
bool ThreadPool::init(size_t num)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (!threads_.empty())
    {
        return false;
    }
    threadNum_ = num;
    return true;
}
void ThreadPool::stop()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        bTerminate_ = true;
        condition_.notify_all();
    }

    for (size_t i = 0; i < threads_.size(); i++)
    {
        if (threads_[i]->joinable())
        {
            threads_[i]->join();
        }
        delete threads_[i];
        threads_[i] = NULL;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    threads_.clear();
}
bool ThreadPool::start()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!threads_.empty())
    {
        return false;
    }

    for (size_t i = 0; i < threadNum_; i++)
    {
        threads_.push_back(new std::thread(&ThreadPool::run, this));
    }
    return true;
}

bool ThreadPool::get(TaskFuncPtr &task)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (tasks_.empty())
    {
        condition_.wait(lock, [this]
                        { return bTerminate_ == true || !tasks_.empty(); });
    }

    if (bTerminate_ == true)
        return false;
    if (!tasks_.empty())
    {
        task = std::move(tasks_.front());
        tasks_.pop();
        return true;
    }
    return false;
}
void ThreadPool::run()
{
    while (!bTerminate_)
    {
        TaskFuncPtr task;
        bool ok = get(task);
        if (ok)
        {
            atomic_++;
            try
            {
                if (task->_expireTime != 0 && task->_expireTime < TNOWMS)
                {
                    // 处理超时任务
                }
                else
                {
                    task->_func();
                }
            }
            catch (...)
            {
            }
            atomic_--;
        }
        if (atomic_ == 0 && tasks_.empty())
        {
            condition_.notify_all(); // 通知生成任务
        }
    }
}

// 1000ms
bool ThreadPool::waitForAllDone(int millsecond)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (tasks_.empty())
    {
        return true;
    }
    if (millsecond < 0)
    {
        condition_.wait(lock, [this]
                        { return tasks_.empty(); });
        return true;
    }
    else
    {
        condition_.wait_for(lock, std::chrono::milliseconds(millsecond), [this]
                            { return tasks_.empty(); });
    }
}
void getNow(timeval *tv)
{
    gettimeofday(tv, 0);
}
int64_t getNowMs()
{
    struct timeval tv;
    getNow(&tv);
    return tv.tv_sec * (int64_t)1000 + tv.tv_usec / 1000;
}