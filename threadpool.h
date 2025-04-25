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
const int THREAD_MAX_IDLE_TIME = 60; // seconds

enum class PoolMode {
	MODE_FIXED, 
	MODE_CACHED, 
};

class Thread {
public:
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}

	~Thread() = default;

	void start() {
		std::thread t(func_, threadId_);
		t.detach();
	}

	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};
int Thread::generateId_ = 0;

class ThreadPool {
public:
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
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	~ThreadPool() {
		isPoolRunning_ = false;
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	bool checkRunningState() const {
		return isPoolRunning_;
	}

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

	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState()) {
			return;
		} 
		taskQueMaxThreshHold_ = threshhold;
	}

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
		isPoolRunning_ = true;
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;
		
		for (int i = 0; i < initThreadSize_; i++) {
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}
		
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start();
			idleThreadSize_++;
		}
	}

	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			// notFull_等待1s，条件依然仍未满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			// 提交任务失败，返回该返回值类型的零值
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;
		notEmpty_.notify_all();

		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << ">>>create new!" << std::endl;
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));

			threads_[threadId]->start();
			curThreadSize_++; 
			idleThreadSize_++; 
		}
		return result;
	}	

private:
	void threadFunc(int threadId) {
		auto lastTime = std::chrono::high_resolution_clock::now();
		for (;;) {
			Task task;
			{
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << std::this_thread::get_id() << "trying to acquire the task." << std::endl;

				while (taskQue_.size() == 0) {
					if (!isPoolRunning_) {
						threads_.erase(threadId);
						std::cout << std::this_thread::get_id() << "exit" << std::endl;
						exitCond_.notify_all();
						return;
					}
					if (poolMode_ == PoolMode::MODE_CACHED) {
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							auto now = std::chrono::high_resolution_clock::now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);

							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								curThreadSize_--;
								idleThreadSize_--;

								threads_.erase(threadId);
								std::cout << std::this_thread::get_id() << "exit" << std::endl;
								return;
							}
						}
					}
					else {
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--;
				std::cout << std::this_thread::get_id() << "Task acquired successfully." << std::endl;
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;
				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}
				notFull_.notify_all();
			}
			if (task != nullptr) {
				task();
			}
			idleThreadSize_++;
			lastTime = std::chrono::high_resolution_clock::now();
		}
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // <threadID, thread_unique_ptr>
	size_t initThreadSize_; // 初始的线程数量
	std::atomic_int idleThreadSize_; // 空闲的线程数量
	std::atomic_int curThreadSize_; // 记录当前线程池里线程的总数
	int threadSizeThreshHold_; // cached模式下，线程数量上限的阈值

	using Task = std::function<void()>; // Task任务 <=> 函数对象
	std::queue<Task> taskQue_; // 任务队列 
	std::atomic_uint taskSize_; // 任务的数量 (线程安全)
	int taskQueMaxThreshHold_; // 任务队列中的任务数量上限的阈值

	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_; // 表示任务队列不空
	std::condition_variable exitCond_; // 用于析构: 等待线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;// 表示当前线程池是否已经启动
};

#endif
