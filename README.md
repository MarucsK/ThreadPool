# ThreadPool
![GitHub License](https://img.shields.io/github/license/xykCs/ThreadPool)

一个基于C++11标准库实现的高性能线程池，支持固定线程数量和动态线程扩容两种模式。提供任务队列管理、自动线程回收、超时等待等特性，可集成到各类C++项目中。
### 整体架构
![image](https://github.com/user-attachments/assets/40297838-98b7-4e18-b8ea-f92d43b9ab1a)
### 支持
- 操作系统
    - linux
    - windows
 - 编译器
     - g++ 4.8及以上
     - Clang 3.3及以上
     - MSVC : vs2015及以上
### QuickStart
##### 依赖：
- C++11标准
##### 引入头文件
```cpp
#include "ThreadPool.h"
```
##### demo：
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




