# ThreadPool
![GitHub License](https://img.shields.io/github/license/xykCs/ThreadPool)

A thread pool implementation based on C++ (requires C++14 or higher for `std::make_unique`). It offers fixed thread count and dynamic thread expansion modes, task queue threshold control, idle thread timeout recovery, task submission timeout handling, and asynchronous result retrieval via `std::future`. Utilizes RAII for resource management.

### Overall Architecture
![image](https://github.com/user-attachments/assets/40297838-98b7-4e18-b8ea-f92d43b9ab1a)
### Supported
- Operating System
    - linux
    - windows
 - Compiler
     - g++ 4.8 and above
     - Clang 3.3 and above
     - MSVC : vs2015 and above
### QuickStart
##### Dependencies:
- C++ Standard: C++14
##### Include Headers:
To use the thread pool, you might need to include headers like:
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

### License
This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for more details.




