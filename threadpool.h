#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<iostream>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include<unordered_map>
#include<future>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 单位：秒

// 线程池支持的两种模式
enum class PoolMode {
	MODE_FIXED, // 固定数量的线程
	MODE_CACHED, // 线程数量可动态增长
};

// 线程类型
class Thread {
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;
	// 线程构造
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}

	// 线程析构
	~Thread() = default;

	// 启动线程
	void start() {
		// 创建一个线程来执行一个线程函数
		std::thread t(func_, threadId_);
		t.detach();
	}

	// 获取线程ID
	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; // 保存线程ID
};
int Thread::generateId_ = 0;

// 线程池类型
class ThreadPool {
public:
	// 线程池构造
	ThreadPool()
		: initThreadSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, taskSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}

	// 线程池的析构
	~ThreadPool() {
		isPoolRunning_ = false;
		// 等待线程池里所有的线程返回
		// 线程有两种状态：阻塞 、正在执行任务
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 唤醒阻塞的 线程池里的线程，让它执行资源释放
		notEmpty_.notify_all();
		// 一直等到没有线程对象存在，析构才结束
		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	// 禁止拷贝构造
	ThreadPool(const ThreadPool&) = delete;

	// 禁止拷贝赋值运算符重载
	ThreadPool& operator=(const ThreadPool&) = delete;

	// 设置线程池的工作模式
	void setMode(PoolMode mode) {
		if (checkRunningState()) {
			return;
		}
		poolMode_ = mode;
	}

	void setThreadSizeThreshHold(int threshhold) {
		if (checkRunningState()) {
			return;
		}
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThreshHold_ = threshhold;
		}
	}

	// 设置任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState()) {
			return;
		} 
		taskQueMaxThreshHold_ = threshhold;
	}

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
		// 设置线程池为已启动状态
		isPoolRunning_ = true;
		// 记录初始的线程数量
		initThreadSize_ = initThreadSize;
		// 记录当前线程总数
		curThreadSize_ = initThreadSize;
		// 创建线程对象
		for (int i = 0; i < initThreadSize_; i++) {
			// 创建Thread线程对象，把线程函数给到Thread对象（通过Thread(ThreadFunc func);构造函数，把bind后的function传给Thread对象,Thread中用func_接收）
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr)); // unique_ptr不允许拷贝构造和赋值，允许移动拷贝构造和赋值，所以做资源转移
		}

		// 启动所有线程
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start(); // 启动线程start()方法，会执行传入该Thread对象的线程函数threadFunc()
			idleThreadSize_++; // 记录空闲线程的数量（光启动还没拿任务执行，所以是空闲）
		}
	}

	// 给线程池提交任务
	// 使用可变参数模板，让submitTask可以接收任意的任务函数和任意数量的参数
	// pool.submitTask(sum1,10,20); 
	// 返回值future<类型>
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		// RType为传入的任务函数func的返回值类型（利用decltype推导出）
		using RType = decltype(func(args...));
		// 等号右侧：创建一个packaged_task，返回值为RType，参数列表写成空
		// 可是传入任务函数参数列表肯定不为空：用bind将所有参数绑定起来
		// task为shared_ptr，它指向这个packaged_task
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		// 对该任务task调用get_future()，定义result接收
		std::future<RType> result = task->get_future();

		// 获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 等待任务队列有空余
		// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			// 表示notFull_等待1s，条件依然仍未满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			// 提交任务失败，返回该返回值类型的零值
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}
		// 如果有空余，把任务放入任务队列中
		// 当前任务task是有返回值的
		// 而任务队列里存的function都没有返回值（std::function<void()>）
		// 我们直接往里存入无返回值、无参的lambda表达式
		// 在lambda函数体内执行这个task：(*task)()
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		// 刚放了任务，任务队列肯定不空了，在notEmpty_上通知
		notEmpty_.notify_all();

		// cached模式下，根据任务数量和空闲线程数量，判断是否需要创建新的线程
		if (poolMode_ == PoolMode::MODE_CACHED // 工作在cached模式
			&& taskSize_ > idleThreadSize_ // 任务数量比空闲线程数量多
			&& curThreadSize_ < threadSizeThreshHold_) // 当前线程总数 小于 上限阈值
		{
			// 创建新线程对象
			std::cout << ">>>create new!" << std::endl;
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			// 启动该线程
			threads_[threadId]->start();
			curThreadSize_++; //线程总数加1
			idleThreadSize_++; // 刚创建肯定是空闲线程
		}
		return result;
	}	

private:
	// 定义线程函数
	void threadFunc(int threadId) {
		auto lastTime = std::chrono::high_resolution_clock::now();
		for (;;) {
			Task task;
			{
				// 先获取锁 拿完任务就应该释放锁，所以添加作用域
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << std::this_thread::get_id() << "尝试获取任务" << std::endl;

				// 如果没任务了，CACHED模式要考虑回收线程，FIXED模式死等任务
				while (taskQue_.size() == 0) {
					if (!isPoolRunning_) {
						threads_.erase(threadId);
						std::cout << std::this_thread::get_id() << "exit" << std::endl;
						exitCond_.notify_all();
						return;
					}
					// cached模式下，如果有多余线程的空闲时间超过60s，应回收该线程
					// 仅回收 超过initThreadSize_数量的线程
					// 当前时间 - 上一次线程执行的时间 > 60s
					if (poolMode_ == PoolMode::MODE_CACHED) {
						// 条件变量notEmpty每1s返回一次，如果是超时返回（返回timeout）
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							auto now = std::chrono::high_resolution_clock::now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							// 如果空闲时间>60s，且目前线程总数>初始线程数
							// 就回收当前线程
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								// 修改 线程数量相关的变量的值
								curThreadSize_--;
								idleThreadSize_--;
								// 把线程对象从线程列表容器中删除
								threads_.erase(threadId);
								std::cout << std::this_thread::get_id() << "exit" << std::endl;
								return;
							}
						}
					}
					// FIXED模式下，线程死等notEmpty条件，等待拿任务
					else {
						// 等待notEmpty_条件
						notEmpty_.wait(lock);
					}
				}
				// 线程拿到任务，不再空闲
				idleThreadSize_--;
				std::cout << std::this_thread::get_id() << "获取任务成功" << std::endl;
				// 从任务队列中取一个任务
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;
				// 如果取完任务后依然有任务，继续通知其他线程来拿任务
				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}
				// 取出一个任务，任务队列肯定不满了，在notFull_上通知
				notFull_.notify_all();
			}
			// 当前线程负责执行这个任务
			if (task != nullptr) {
				task();
			}
			// 线程执行完当前任务，重新变为空闲
			idleThreadSize_++;
			// 更新线程执行完任务花费的时间
			lastTime = std::chrono::high_resolution_clock::now();
		}
	}

	// 检查pool的运行状态
	bool checkRunningState() const {
		return isPoolRunning_;
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表 first:线程ID  second:指向线程对象的unique_ptr
	size_t initThreadSize_; // 初始的线程数量
	std::atomic_int idleThreadSize_; // 空闲的线程数量
	std::atomic_int curThreadSize_; // 记录当前线程池里线程的总数
	int threadSizeThreshHold_; // cached模式下，线程数量上限的阈值

	using Task = std::function<void()>; // Task任务 《=》 函数对象
	std::queue<Task> taskQue_; // 任务队列 
	std::atomic_uint taskSize_; // 任务的数量    （线程拿任务要减，用户提交任务要加，所以要保证线程安全）
	int taskQueMaxThreshHold_; // 任务队列中的任务数量上限的阈值

	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_; // 表示任务队列不空
	std::condition_variable exitCond_; // 用于析构：等待线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;// 表示当前线程池是否已经启动
};

#endif
