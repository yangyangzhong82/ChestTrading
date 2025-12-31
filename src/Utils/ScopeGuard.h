#pragma once

#include <functional>
#include <vector>

namespace CT {

/**
 * @brief RAII 风格的作用域守卫，用于自动执行清理/回滚操作
 *
 * 使用示例：
 * @code
 * ScopeGuard guard;
 * guard.addRollback([&]() { Economy::addMoney(buyer, totalPrice); });
 * guard.addRollback([&]() { addItemsToChest(region, mainPos, itemNbt, quantity); });
 *
 * // ... 执行操作 ...
 *
 * if (success) {
 *     guard.dismiss(); // 成功时取消回滚
 * }
 * // guard 析构时会自动执行所有未 dismiss 的回滚操作
 * @endcode
 */
class ScopeGuard {
public:
    using RollbackFunc = std::function<void()>;

    ScopeGuard() = default;

    // 禁止拷贝
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    // 允许移动
    ScopeGuard(ScopeGuard&& other) noexcept
    : mRollbacks(std::move(other.mRollbacks)),
      mDismissed(other.mDismissed) {
        other.mDismissed = true;
    }

    ScopeGuard& operator=(ScopeGuard&& other) noexcept {
        if (this != &other) {
            executeRollbacks();
            mRollbacks       = std::move(other.mRollbacks);
            mDismissed       = other.mDismissed;
            other.mDismissed = true;
        }
        return *this;
    }

    ~ScopeGuard() { executeRollbacks(); }

    /**
     * @brief 添加一个回滚操作（后进先出，LIFO 顺序执行）
     * @param rollback 回滚函数
     */
    void addRollback(RollbackFunc rollback) {
        if (rollback) {
            mRollbacks.push_back(std::move(rollback));
        }
    }

    /**
     * @brief 取消所有回滚操作（表示操作成功）
     */
    void dismiss() { mDismissed = true; }

    /**
     * @brief 手动执行所有回滚操作
     */
    void executeRollbacks() {
        if (!mDismissed && !mRollbacks.empty()) {
            // 反向执行（后添加的先执行，LIFO）
            for (auto it = mRollbacks.rbegin(); it != mRollbacks.rend(); ++it) {
                try {
                    (*it)();
                } catch (...) {
                    // 回滚操作不应抛出异常，但为了安全捕获所有异常
                    // 这里可以记录日志，但不能再次抛出
                }
            }
            mRollbacks.clear();
        }
        mDismissed = true;
    }

    /**
     * @brief 检查是否已被 dismiss
     */
    bool isDismissed() const { return mDismissed; }

private:
    std::vector<RollbackFunc> mRollbacks;
    bool                      mDismissed = false;
};

/**
 * @brief 简化的作用域守卫构造助手
 *
 * 使用示例：
 * @code
 * auto guard = makeScopeGuard([&]() { cleanup(); });
 * @endcode
 */
template <typename Func>
ScopeGuard makeScopeGuard(Func&& func) {
    ScopeGuard guard;
    guard.addRollback(std::forward<Func>(func));
    return guard;
}

} // namespace CT
