# ThreadPool
一个基于C++11的跨平台高性能动态线程池，支持动态线程扩缩容和智能任务调度。，可高效管理多任务并发执行。
### 功能特性：
- 双工作模式支持  
    - FIXED模式: 固定数量工作线程
    - CACHED模式: 线程数量动态扩容收缩


### Qucik
##### 依赖要求：
- 支持C++20标准
- Linux

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

### 整体架构
![image](https://github.com/user-attachments/assets/40297838-98b7-4e18-b8ea-f92d43b9ab1a)


