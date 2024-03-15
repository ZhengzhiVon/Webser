# Webserver项目练习

[参考：TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

[参考](https://mp.weixin.qq.com/s?__biz=MzAxNzU2MzcwMw==&mid=2649274278&idx=3&sn=5840ff698e3f963c7855d702e842ec47&chksm=83ffbefeb48837e86fed9754986bca6db364a6fe2e2923549a378e8e5dec6e3cf732cdb198e2&scene=0&xtrack=1#rd)

# 框架设计理解



# 模块设计

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

# 校验 & 数据库连接池

数据库连接池

> - 单例模式，保证唯一
> - list实现连接池
> - 连接池为静态大小
> - 互斥锁实现线程安全

校验

> - HTTP请求采用POST方式
> - 登录用户名和密码校验
> - 用户注册及多线程注册安全

# 线程同步机制包装类

多线程同步，确保任一时刻只能有一个线程能进入关键代码段.

> - 信号量
> - 互斥锁
> - 条件变量