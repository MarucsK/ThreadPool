# ThreadPool
作为五大池(进程池、线程池、内存池、数据库连接池、对象池)之一，不管是客户端程序，还是后台服务程序，线程池都是提高业务处理能力的必备模块。本项目实现一个基于C++11的跨平台动态线程池实现，支持固定线程数量和动态资源调整两种工作模式，可高效管理多任务并发执行。
### 功能特性：
(1)双模式支持  
- FIXED模式: 固定数量工作线程
- CACHED模式: 线程数量动态扩容收缩
(2)智能资源管理  
- 自动回收空闲线程（60秒无任务）
- 任务队列容量保护机制（默认1024任务上限）
- 线程数量动态调整（根据任务压力）
(3)高性能特性  
- 无锁任务队列设计（基于互斥锁+条件变量）
- 支持批量任务唤醒机制
- 线程安全的任务提交接口
(4)Future机制  
- 支持通过std::future获取任务返回值
- 任务超时提交检测（1秒队列满快速失败）

### 快速开始
```cpp
#include <iostream>
#include <future>
#include <chrono>
using namespace std;

#include "threadpool.h"

int sum1(int a, int b)
{
    this_thread::sleep_for(chrono::seconds(3));
    return a + b;
}
int sum2(int a, int b, int c)
{
    this_thread::sleep_for(chrono::seconds(3));
    return a + b + c;
}
int main()
{
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(8);

    future<int> r1 = pool.submitTask(sum1, 1, 2);
    future<int> r2 = pool.submitTask(sum2, 1, 2, 3);
    future<int> r3 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> r4 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);
    future<int> r5 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
            sum += i;
        return sum;
        }, 1, 100);

    cout << r1.get() << endl;
    cout << r2.get() << endl;
    cout << r3.get() << endl;
    cout << r4.get() << endl;
    cout << r5.get() << endl;
}
```

### 整体架构
![image](https://github.com/user-attachments/assets/f059a903-4003-42bb-8fca-367a08c8d284)
