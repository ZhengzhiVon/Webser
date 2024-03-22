#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../Coon_mysql/sql_connection_pool.h"

template <typename T>
class threadpool {
public:
    // 构造函数，初始化线程池
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    
    // 析构函数，销毁线程池
    ~threadpool();

    // 向工作队列添加请求，state表示请求的状态
    bool append(T *request, int state);

    // 向工作队列添加请求，不指定状态
    bool append_p(T *request);

private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void *worker(void *arg);

    // 真正的工作函数，执行任务
    void run();

private:
    int m_thread_number;          // 线程池中的线程数
    int m_max_requests;           // 请求队列中允许的最大请求数
    pthread_t *m_threads;         // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;   // 请求队列
    locker m_queuelocker;         // 保护请求队列的互斥锁
    sem m_queuestat;              // 是否有任务需要处理的信号量
    connection_pool *m_connPool;  // 数据库连接池
    int m_actor_model;            // 模型切换标志
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests)
    : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool) {
    // 检查线程数和最大请求数是否合法
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception(); // 如果不合法，抛出异常
    
    // 创建线程数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception(); // 内存分配失败，抛出异常
    
    // 创建工作线程并分离
    for (int i = 0; i < thread_number; ++i) {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            // 创建线程失败，释放线程数组内存并抛出异常
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            // 分离线程失败，释放线程数组内存并抛出异常
            delete[] m_threads;
            throw std::exception();
        }
    }
}


template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock(); // 加锁，保护工作队列
    if (m_workqueue.size() >= m_max_requests) { // 检查工作队列是否已满
        m_queuelocker.unlock(); // 如果队列已满，解锁
        return false; // 返回 false，表示添加请求失败
    }
    request->m_state = state; // 设置请求状态
    m_workqueue.push_back(request); // 将请求添加到工作队列尾部
    m_queuelocker.unlock(); // 解锁
    m_queuestat.post(); // 发送信号通知工作线程有任务需要处理
    return true; // 返回 true，表示添加请求成功
}


template <typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock(); // 加锁，保护工作队列
    if (m_workqueue.size() >= m_max_requests) { // 检查工作队列是否已满
        m_queuelocker.unlock(); // 如果队列已满，解锁
        return false; // 返回 false，表示添加请求失败
    }
    m_workqueue.push_back(request); // 将请求添加到工作队列尾部
    m_queuelocker.unlock(); // 解锁
    m_queuestat.post(); // 发送信号通知工作线程有任务需要处理
    return true; // 返回 true，表示添加请求成功
}


template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg; // 将传入的参数转换为线程池对象指针
    pool->run(); // 调用线程池对象的 run 方法执行工作函数
    return pool; // 返回线程池对象指针
}


template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait(); // 等待有任务需要处理的信号

        m_queuelocker.lock(); // 加锁，保护工作队列
        if (m_workqueue.empty()) { // 检查工作队列是否为空
            m_queuelocker.unlock(); // 如果队列为空，解锁并继续等待下一个信号
            continue;
        }
        T *request = m_workqueue.front(); // 获取请求队列中的第一个任务
        m_workqueue.pop_front(); // 移除队列中的第一个任务
        m_queuelocker.unlock(); // 解锁

        if (!request) // 如果请求为空，继续处理下一个请求
            continue;

        // 根据模型切换标志选择相应的处理方式
        if (1 == m_actor_model) {
            if (0 == request->m_state) { // 读模式
                if (request->read_once()) { // 读取数据成功
                    request->improv = 1; // 标记请求已经被处理
                    connectionRAII mysqlcon(&request->mysql, m_connPool); // 自动释放数据库连接
                    request->process(); // 处理请求
                } else { // 读取数据失败
                    request->improv = 1; // 标记请求已经被处理
                    request->timer_flag = 1; // 标记定时器超时
                }
            } else { // 写模式
                if (request->write()) { // 写入数据成功
                    request->improv = 1; // 标记请求已经被处理
                } else { // 写入数据失败
                    request->improv = 1; // 标记请求已经被处理
                    request->timer_flag = 1; // 标记定时器超时
                }
            }
        } else { // 不切换模型，直接处理请求
            connectionRAII mysqlcon(&request->mysql, m_connPool); // 自动释放数据库连接
            request->process(); // 处理请求
        }
    }
}




#endif