#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量类
class sem
{
public:
    // 构造函数，初始化信号量
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception(); // 如果初始化失败，抛出异常
        }
    }
    // 构造函数，初始化信号量为给定的初始值
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception(); // 如果初始化失败，抛出异常
        }
    }
    // 析构函数，销毁信号量
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    // 等待信号量
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    // 发送信号量
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem; // 信号量变量
};

// 互斥锁类
class locker
{
public:
    // 构造函数，初始化互斥锁
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception(); // 如果初始化失败，抛出异常
        }
    }
    // 析构函数，销毁互斥锁
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    // 加锁
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    // 解锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获取互斥锁变量的指针
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex; // 互斥锁变量
};

// 条件变量类
class cond
{
public:
    // 构造函数，初始化条件变量
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception(); // 如果初始化失败，抛出异常
        }
    }
    // 析构函数，销毁条件变量
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    // 等待条件变量，需要传入一个互斥锁
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex); // 等待条件变量
        return ret == 0;
    }
    // 带超时的等待条件变量，需要传入一个互斥锁和一个时间结构体
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t); // 带超时的等待条件变量
        return ret == 0;
    }
    // 发送信号给一个等待的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    // 广播信号给所有等待的线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond; // 条件变量变量
};

#endif