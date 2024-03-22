# Webserver项目练习

[参考：TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

[参考](https://mp.weixin.qq.com/s?__biz=MzAxNzU2MzcwMw==&mid=2649274278&idx=3&sn=5840ff698e3f963c7855d702e842ec47&chksm=83ffbefeb48837e86fed9754986bca6db364a6fe2e2923549a378e8e5dec6e3cf732cdb198e2&scene=0&xtrack=1#rd)

# 框架设计理解



# 模块设计

## 线程同步机制封装类（信号量&互斥锁&条件变量）

- 实现多线程同步，通过锁机制，确保任一时刻只能有一个线程能进入关键代码段

- 类中主要是Linux下三种锁进行封装，将锁的创建于销毁函数封装在类的构造与析构函数中，实现RAII机制

### 信号量类

[semaphore.h](https://zhuanlan.zhihu.com/p/512969481)

- `sem_init`函数用于初始化一个未命名的信号量
- `sem_destory`函数用于销毁信号量
- `sem_wait`函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞
- `sem_post`函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程

```cpp
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
```

### 互斥量类

- `pthread_mutex_init`函数用于初始化互斥锁
- `pthread_mutex_destory`函数用于销毁互斥锁
- `pthread_mutex_lock`函数以原子操作方式给互斥锁加锁
- `pthread_mutex_unlock`函数以原子操作方式给互斥锁解锁

```cpp
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
```

### 条件变量类

- `pthread_cond_init`函数用于初始化条件变量
- `pthread_cond_destory`函数销毁条件变量
- `pthread_cond_broadcast`函数以广播的方式唤醒**所有**等待目标条件变量的线程
- `pthread_cond_wait`函数用于等待目标条件变量.该函数调用时需要传入 **mutex参数(加锁的互斥锁)** ,函数执行时,先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁,当函数成功返回为0时,互斥锁会再次被锁上. **也就是说函数内部会有一次解锁和加锁操作**.

```cpp
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
```

## 数据库连接池

[数据库连接池](https://mp.weixin.qq.com/s?__biz=MzAxNzU2MzcwMw==&mid=2649274326&idx=1&sn=5af78e2bf6552c46ae9ab2aa22faf839&chksm=83ffbe8eb4883798c3abb82ddd124c8100a39ef41ab8d04abe42d344067d5e1ac1b0cac9d9a3&cur_album_id=1339230165934882817&scene=189#wechat_redirect)

### 数据库连接池介绍

池是一组资源的集合，这组资源在服务器启动之初就被完全创建好并初始化。通俗来说，池是资源的容器，本质上是对**资源的复用**。

数据库连接池中的资源为一组数据库连接，<u>由程序动态地对池中的连接进行使用，释放</u>。

当系统开始处理客户请求的时候，如果它需要相关的资源，可以直接从池中获取，无需动态分配；当服务器处理完一个客户连接后,可以把相关的资源放回池中，无需执行系统调用释放资源。

> **为什么要创建连接池？**
>
> 从一般流程中可以看出，若系统需要频繁访问数据库，则需要频繁创建和断开数据库连接，而创建数据库连接是一个很耗时的操作，也容易对数据库造成安全隐患。
>
> 在程序初始化的时候，集中创建多个数据库连接，并把他们集中管理，供程序使用，可以保证较快的数据库读写速度，更加安全可靠。

### 设计开发

数据库模块分为两部分，其一是**数据库连接池的定义**，其二是**利用连接池完成登录和注册的校验功能**。

**工作线程从数据库连接池取得一个连接，访问数据库中的数据，访问完毕后将连接交还连接池**。

- **单例模式创建**，数据库连接池的单例实现。
- **连接池代码实现**，结合代码对连接池的外部访问接口进行详解。
- **RAII机制释放数据库连接**，描述连接释放的封装逻辑。

### 完整代码

`sql_connection_pool.h`

```cpp
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
    public:
        MYSQL *GetConnection(); //获取数据库连接
        bool ReleaseConnection(MYSQL *conn);//释放连接
        int GetFreeConn();  //获取连接
        void DestroyPool();//销毁所有连接

        //单例模式
        static connection_pool *GetInstance();
        void init(string url,string User,string PassWord,string DataBaseName,int Port,int MaxConn,int close_log);
    private:
        connection_pool();
        ~connection_pool();

        int m_MaxConn;  //最大连接数
        int m_FreeConn; //当前空闲连接数
        int m_CurConn;  //当前已使用的连接数
        locker lock;
        list<MYSQL *> connList;//连接池
        sem reserve;
    public:
        string m_url;   //主机地址
        string m_Port;  //数据库端口号
        string m_User;  //数据库用户名
        string m_PassWord;  //数据库密码
        string m_DataBaseName;  //数据库名
        int m_close_log;    //是否关闭日志(日志开关)
};

class connectionRAII
{
    public:
        connectionRAII(MYSQL **CON,connection_pool *connPool);
        ~connectionRAII();
    private:
        MYSQL *conRAII;
        connection_pool *poolRAII;
};

#endif
```

`sql_connection_pool.cpp`

```cpp
#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();
	
	lock.lock();

	con = connList.front();
	connList.pop_front();

	--m_FreeConn;
	++m_CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}
```

### 单例模式创建连接池

使用**局部静态变量懒汉模式**创建连接池。

```cpp
class connection_pool
{
public:
 	//局部静态变量单例模式
    static connection_pool *GetInstance();  
private:
     connection_pool();
     ~connection_pool();
}；
    
//实现
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}
```

### 连接池主要功能

连接池的功能主要有：**初始化，获取连接、释放连接，销毁连接池**。

#### 初始化



# 开发日志

开发过程记录。

| 时间 | 任务记录 |
| :--: | :------: |
|      |          |
|      |          |
|      |          |
|      |          |
|      |          |

