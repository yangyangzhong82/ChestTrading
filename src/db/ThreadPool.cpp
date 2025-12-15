#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numThreads) : mStop(false), mActiveTasks(0) {

    // 确保至少有一个线程
    if (numThreads == 0) {
        numThreads = 1;
    }

    // 创建工作线程
    for (size_t i = 0; i < numThreads; ++i) {
        mWorkers.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->mQueueMutex);

                    // 等待任务或停止信号
                    this->mCondition.wait(lock, [this] { return this->mStop || !this->mTasks.empty(); });

                    // 如果停止且队列为空，退出线程
                    if (this->mStop && this->mTasks.empty()) {
                        return;
                    }

                    // 获取任务
                    if (!this->mTasks.empty()) {
                        task = std::move(this->mTasks.front());
                        this->mTasks.pop();
                        this->mActiveTasks++;
                    }
                }

                // 执行任务
                if (task) {
                    try {
                        task();
                    } catch (...) {
                        // 忽略异常，确保计数正确递减
                    }
                    this->mActiveTasks--;

                    // 通知等待任务完成的线程
                    this->mTasksCompleteCondition.notify_all();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }

    mCondition.notify_all();
    mTasksCompleteCondition.notify_all();

    // 等待所有线程完成
    for (std::thread& worker : mWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

size_t ThreadPool::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mQueueMutex));
    return mTasks.size() + mActiveTasks.load();
}

void ThreadPool::waitForAllTasks() {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    mTasksCompleteCondition.wait(lock, [this] { return mStop || (mTasks.empty() && mActiveTasks.load() == 0); });
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }
    mCondition.notify_all();
}
