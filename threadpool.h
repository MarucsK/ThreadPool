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
const int THREAD_MAX_IDLE_TIME = 60; // ��λ����

// �̳߳�֧�ֵ�����ģʽ
enum class PoolMode {
	MODE_FIXED, // �̶��������߳�
	MODE_CACHED, // �߳������ɶ�̬����
};

// �߳�����
class Thread {
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void(int)>;
	// �̹߳���
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}

	// �߳�����
	~Thread() = default;

	// �����߳�
	void start() {
		// ����һ���߳���ִ��һ���̺߳���
		std::thread t(func_, threadId_);
		t.detach();
	}

	// ��ȡ�߳�ID
	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_; // �����߳�ID
};
int Thread::generateId_ = 0;

// �̳߳�����
class ThreadPool {
public:
	// �̳߳ع���
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

	// �̳߳ص�����
	~ThreadPool() {
		isPoolRunning_ = false;
		// �ȴ��̳߳������е��̷߳���
		// �߳�������״̬������ ������ִ������
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// ���������� �̳߳�����̣߳�����ִ����Դ�ͷ�
		notEmpty_.notify_all();
		// һֱ�ȵ�û���̶߳�����ڣ������Ž���
		exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
	}

	// ��ֹ��������
	ThreadPool(const ThreadPool&) = delete;

	// ��ֹ������ֵ���������
	ThreadPool& operator=(const ThreadPool&) = delete;

	// �����̳߳صĹ���ģʽ
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

	// �����������������ֵ
	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState()) {
			return;
		} 
		taskQueMaxThreshHold_ = threshhold;
	}

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
		// �����̳߳�Ϊ������״̬
		isPoolRunning_ = true;
		// ��¼��ʼ���߳�����
		initThreadSize_ = initThreadSize;
		// ��¼��ǰ�߳�����
		curThreadSize_ = initThreadSize;
		// �����̶߳���
		for (int i = 0; i < initThreadSize_; i++) {
			// ����Thread�̶߳��󣬰��̺߳�������Thread����ͨ��Thread(ThreadFunc func);���캯������bind���function����Thread����,Thread����func_���գ�
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr)); // unique_ptr������������͸�ֵ�������ƶ���������͸�ֵ����������Դת��
		}

		// ���������߳�
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start(); // �����߳�start()��������ִ�д����Thread������̺߳���threadFunc()
			idleThreadSize_++; // ��¼�����̵߳���������������û������ִ�У������ǿ��У�
		}
	}

	// ���̳߳��ύ����
	// ʹ�ÿɱ����ģ�壬��submitTask���Խ�������������������������Ĳ���
	// pool.submitTask(sum1,10,20); 
	// ����ֵfuture<����>
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		// RTypeΪ�����������func�ķ���ֵ���ͣ�����decltype�Ƶ�����
		using RType = decltype(func(args...));
		// �Ⱥ��Ҳࣺ����һ��packaged_task������ֵΪRType�������б�д�ɿ�
		// ���Ǵ��������������б�϶���Ϊ�գ���bind�����в���������
		// taskΪshared_ptr����ָ�����packaged_task
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		// �Ը�����task����get_future()������result����
		std::future<RType> result = task->get_future();

		// ��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// �ȴ���������п���
		// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			// ��ʾnotFull_�ȴ�1s��������Ȼ��δ����
			std::cerr << "task queue is full, submit task fail." << std::endl;
			// �ύ����ʧ�ܣ����ظ÷���ֵ���͵���ֵ
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}
		// ����п��࣬������������������
		// ��ǰ����task���з���ֵ��
		// �������������function��û�з���ֵ��std::function<void()>��
		// ����ֱ����������޷���ֵ���޲ε�lambda���ʽ
		// ��lambda��������ִ�����task��(*task)()
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		// �շ�������������п϶������ˣ���notEmpty_��֪ͨ
		notEmpty_.notify_all();

		// cachedģʽ�£��������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��߳�
		if (poolMode_ == PoolMode::MODE_CACHED // ������cachedģʽ
			&& taskSize_ > idleThreadSize_ // ���������ȿ����߳�������
			&& curThreadSize_ < threadSizeThreshHold_) // ��ǰ�߳����� С�� ������ֵ
		{
			// �������̶߳���
			std::cout << ">>>create new!" << std::endl;
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			// �������߳�
			threads_[threadId]->start();
			curThreadSize_++; //�߳�������1
			idleThreadSize_++; // �մ����϶��ǿ����߳�
		}
		return result;
	}	

private:
	// �����̺߳���
	void threadFunc(int threadId) {
		auto lastTime = std::chrono::high_resolution_clock::now();
		for (;;) {
			Task task;
			{
				// �Ȼ�ȡ�� ���������Ӧ���ͷ������������������
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;

				// ���û�����ˣ�CACHEDģʽҪ���ǻ����̣߳�FIXEDģʽ��������
				while (taskQue_.size() == 0) {
					if (!isPoolRunning_) {
						threads_.erase(threadId);
						std::cout << std::this_thread::get_id() << "exit" << std::endl;
						exitCond_.notify_all();
						return;
					}
					// cachedģʽ�£�����ж����̵߳Ŀ���ʱ�䳬��60s��Ӧ���ո��߳�
					// ������ ����initThreadSize_�������߳�
					// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
					if (poolMode_ == PoolMode::MODE_CACHED) {
						// ��������notEmptyÿ1s����һ�Σ�����ǳ�ʱ���أ�����timeout��
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
							auto now = std::chrono::high_resolution_clock::now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							// �������ʱ��>60s����Ŀǰ�߳�����>��ʼ�߳���
							// �ͻ��յ�ǰ�߳�
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								// �޸� �߳�������صı�����ֵ
								curThreadSize_--;
								idleThreadSize_--;
								// ���̶߳�����߳��б�������ɾ��
								threads_.erase(threadId);
								std::cout << std::this_thread::get_id() << "exit" << std::endl;
								return;
							}
						}
					}
					// FIXEDģʽ�£��߳�����notEmpty�������ȴ�������
					else {
						// �ȴ�notEmpty_����
						notEmpty_.wait(lock);
					}
				}
				// �߳��õ����񣬲��ٿ���
				idleThreadSize_--;
				std::cout << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
				// �����������ȡһ������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;
				// ���ȡ���������Ȼ�����񣬼���֪ͨ�����߳���������
				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}
				// ȡ��һ������������п϶������ˣ���notFull_��֪ͨ
				notFull_.notify_all();
			}
			// ��ǰ�̸߳���ִ���������
			if (task != nullptr) {
				task();
			}
			// �߳�ִ���굱ǰ�������±�Ϊ����
			idleThreadSize_++;
			// �����߳�ִ�������񻨷ѵ�ʱ��
			lastTime = std::chrono::high_resolution_clock::now();
		}
	}

	// ���pool������״̬
	bool checkRunningState() const {
		return isPoolRunning_;
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б� first:�߳�ID  second:ָ���̶߳����unique_ptr
	size_t initThreadSize_; // ��ʼ���߳�����
	std::atomic_int idleThreadSize_; // ���е��߳�����
	std::atomic_int curThreadSize_; // ��¼��ǰ�̳߳����̵߳�����
	int threadSizeThreshHold_; // cachedģʽ�£��߳��������޵���ֵ

	using Task = std::function<void()>; // Task���� ��=�� ��������
	std::queue<Task> taskQue_; // ������� 
	std::atomic_uint taskSize_; // ���������    ���߳�������Ҫ�����û��ύ����Ҫ�ӣ�����Ҫ��֤�̰߳�ȫ��
	int taskQueMaxThreshHold_; // ��������е������������޵���ֵ

	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable notEmpty_; // ��ʾ������в���
	std::condition_variable exitCond_; // �����������ȴ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_;// ��ʾ��ǰ�̳߳��Ƿ��Ѿ�����
};

#endif
