# 半同步/半反应堆线程池

使用一个工作队列完全解除了主线程和工作线程的耦合关系：主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行它。

> - 同步I/O模拟proactor模式
> - 半同步/半反应堆
> - 线程池

## 完整代码

```cpp
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
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 发送信号通知工作线程有任务需要处理
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 发送信号通知工作线程有任务需要处理
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run(); // 执行工作函数
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait(); // 等待有任务需要处理的信号
        
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front(); // 获取请求队列中的第一个任务
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        
        if (!request)
            continue;

        // 根据模型切换标志选择相应的处理方式
        if (1 == m_actor_model) {
            if (0 == request->m_state) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool); // 自动释放数据库连接
                    request->process(); // 处理请求
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else {
            connectionRAII mysqlcon(&request->mysql, m_connPool); // 自动释放数据库连接
            request->process(); // 处理请求
        }
    }
}

#endif
```

## 代码解析

### 构造函数`threadpool<T>::threadpool`

- `threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests)` 是线程池的构造函数，用于初始化线程池。
- `actor_model` 表示请求处理模型，`connPool` 是数据库连接池的指针，`thread_number` 是线程池中线程的数量，`max_requests` 是请求队列中最多允许的、等待处理的请求的数量。
- 在构造函数中，首先对输入的线程数量和请求数量进行合法性检查，如果不合法，则抛出异常。
- 然后，分配存储线程标识符的数组 `m_threads`，如果内存分配失败，则抛出异常。
- 接下来，通过循环创建线程并分离，线程执行的函数是 `worker`，并将当前对象的指针传递给线程函数。
- 如果创建线程失败，则释放线程数组内存，并抛出异常。

```cpp
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
```

### 析构函数 `threadpool<T>::~threadpool` 

- `threadpool<T>::~threadpool()` 是线程池的析构函数，用于销毁线程池。
- 在析构函数中，只需释放存储线程标识符的数组 `m_threads` 的内存即可。
- 由于 `m_threads` 是通过 `new` 分配的数组内存，因此在析构函数中需要使用 `delete[]` 来释放该内存。

```cpp
template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads; // 释放线程数组内存
}
```

### 添加请求函数 `threadpool<T>::append`& `threadpool<T>::append_p`  

**添加请求函数`threadpool<T>::append`**。它用于向工作队列中添加任务，并在添加完毕后发送信号通知工作线程有任务需要处理。函数的作用是确保工作队列不会超过最大容量，如果超过了最大容量，则添加失败，否则将请求添加到队列尾部并返回成功。

```cpp
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
```

**添加请求函数`threadpool<T>::append_p`** 。与 `append` 函数类似，它也用于向工作队列中添加任务，并在添加完毕后发送信号通知工作线程有任务需要处理。与 `append` 函数不同之处在于，`append_p` 函数不需要设置请求状态，直接将请求添加到队列尾部。

```cpp
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

```

### 工作线程函数 `threadpool<T>::worker` 

工作线程函数 `threadpool<T>::worker` 。每个工作线程都执行此函数，函数接收一个 `void*` 参数，即线程池对象的指针。在函数内部，首先将传入的参数转换为线程池对象指针，然后调用线程池对象的 `run` 方法执行工作函数。最后返回线程池对象指针。

```cpp
template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg; // 将传入的参数转换为线程池对象指针
    pool->run(); // 调用线程池对象的 run 方法执行工作函数
    return pool; // 返回线程池对象指针
}
```

### 线程池工作函数 `threadpool<T>::run` 

**线程池工作函数 `threadpool<T>::run`** ，即工作线程从工作队列中取出任务并执行的过程。函数会在接收到信号通知有任务需要处理时进入工作循环，然后从工作队列中取出第一个任务，并根据请求的模式切换标志选择相应的处理方式。如果是读模式，则尝试读取数据并处理请求；如果是写模式，则尝试写入数据并处理请求。处理完毕后，标记请求已经被处理，然后继续循环等待下一个信号。

```cpp
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
```

