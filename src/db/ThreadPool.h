#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>

/**
 * @brief 线程池类，用于异步执行数据库操作
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * @param numThreads 线程数量，默认为硬件并发数
     */
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    
    /**
     * @brief 析构函数，会等待所有任务完成
     */
    ~ThreadPool();

    // 删除拷贝构造和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务到线程池
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param f 要执行的函数
     * @param args 函数参数
     * @return std::future 用于获取任务结果
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief 获取线程池中的线程数量
     */
    size_t getThreadCount() const { return mWorkers.size(); }

    /**
     * @brief 获取待处理任务数量
     */
    size_t getPendingTaskCount() const;

    /**
     * @brief 等待所有任务完成
     */
    void waitForAllTasks();

    /**
     * @brief 停止线程池（不等待任务完成）
     */
    void stop();

private:
    // 工作线程
    std::vector<std::thread> mWorkers;
    
    // 任务队列
    std::queue<std::function<void()>> mTasks;
    
    // 同步原语
    mutable  std::mutex mQueueMutex;
    std::condition_variable mCondition;
    std::condition_variable mTasksCompleteCondition;
    
    // 停止标志
    std::atomic<bool> mStop;
    
    // 活跃任务计数
    std::atomic<size_t> mActiveTasks;
};

// 模板函数实现
template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        
        // 不允许在停止后添加新任务
        if (mStop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        mTasks.emplace([task]() { (*task)(); });
    }
    
    mCondition.notify_one();
    return res;
}
