#pragma once

#include "ll/api/coro/CoroTask.h"

#include <atomic>
#include <optional>

namespace CT {

/**
 * @brief 箱子商店过期服务
 *
 * 根据配置中的 chestExpirySettings 周期性检查商店箱子是否已超过有效期，
 * 过期后将其从 chests 表中移除（级联清理商品/委托/分享/限购/动态价格等），
 * 并同步移除悬浮字、使缓存失效，使其变回普通箱子。
 *
 * 过期返还费用（可选）会按 chestRemovalRefunds 配置返还给箱子主人。
 */
class ChestExpiryService {
public:
    static ChestExpiryService& getInstance();

    ChestExpiryService(const ChestExpiryService&)            = delete;
    ChestExpiryService& operator=(const ChestExpiryService&) = delete;

    // 立即执行一次过期检查（同步、阻塞当前线程直到完成）
    void checkAndExpire();

    // 启动后台周期性过期检查协程
    void startPeriodicCheck();

    // 停止后台周期性过期检查协程
    void stopPeriodicCheck();

private:
    ChestExpiryService() = default;

    ll::coro::CoroTask<> periodicCheckCoroutine();

    std::optional<ll::coro::CoroTask<>> mCheckTask;
    std::atomic<bool>                   mShouldStop{false};
};

} // namespace CT
