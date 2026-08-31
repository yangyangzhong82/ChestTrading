#include "service/ChestExpiryService.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "Utils/MoneyFormat.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "form/FormUtils.h"
#include "logger.h"
#include "ll/api/coro/SleepAwaiter.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "repository/ChestRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/TextService.h"

#include <chrono>
#include <string>

namespace CT {

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;

double getChestRemovalRefund(ChestType type) {
    const auto& refunds = ConfigManager::getInstance().get().chestRemovalRefunds;
    switch (type) {
    case ChestType::Locked:
        return refunds.lockedChestRefund;
    case ChestType::Public:
        return refunds.publicChestRefund;
    case ChestType::RecycleShop:
        return refunds.recycleShopRefund;
    case ChestType::Shop:
        return refunds.shopRefund;
    case ChestType::AdminShop:
        return refunds.adminShopRefund;
    case ChestType::AdminRecycle:
        return refunds.adminRecycleRefund;
    default:
        return 0.0;
    }
}

std::string dimIdToLocalizedString(int dimId) {
    auto& i18n = I18nService::getInstance();
    switch (dimId) {
    case 0:
        return i18n.get("dimension.overworld");
    case 1:
        return i18n.get("dimension.nether");
    case 2:
        return i18n.get("dimension.end");
    default:
        return i18n.get("dimension.unknown");
    }
}

Player* findOnlinePlayerByUuidString(Level& level, const std::string& playerUuid) {
    Player* target = nullptr;
    level.forEachPlayer([&](Player& player) {
        if (player.getUuid().asString() == playerUuid) {
            target = &player;
            return false;
        }
        return true;
    });
    return target;
}

Player* findOnlinePlayerInDimension(Level& level, int dimId) {
    Player* target = nullptr;
    level.forEachPlayer([&](Player& player) {
        if (player.getDimensionId().value() == dimId) {
            target = &player;
            return false;
        }
        return true;
    });
    return target;
}

void invalidateChestCacheAround(BlockPos pos, int dimId) {
    auto& cache = ChestCacheManager::getInstance();
    cache.invalidateCache(pos, dimId);
    cache.invalidateCache(BlockPos{pos.x + 1, pos.y, pos.z}, dimId);
    cache.invalidateCache(BlockPos{pos.x - 1, pos.y, pos.z}, dimId);
    cache.invalidateCache(BlockPos{pos.x, pos.y, pos.z + 1}, dimId);
    cache.invalidateCache(BlockPos{pos.x, pos.y, pos.z - 1}, dimId);
}

void clearExpiredChestRelatedQueryCaches() {
    auto& db = Sqlite3Wrapper::getInstance();
    db.clearCacheForTable("chests");
    db.clearCacheForTable("shared_chests");
    db.clearCacheForTable("shop_items");
    db.clearCacheForTable("recycle_shop_items");
    db.clearCacheForTable("dynamic_pricing");
    db.clearCacheForTable("player_limits");
    db.clearCacheForTable("player_limit_resets");
}

bool shouldKeepExpiredShopAfterInventoryCheck(const ChestData& chest, Level* level) {
    if (chest.type != ChestType::Shop) {
        return false;
    }

    auto items = ShopRepository::getInstance().findAllItems(chest.pos, chest.dimId);
    if (items.empty()) {
        return false;
    }

    if (!level) {
        logger.debug(
            "箱子过期检查：无法获取 Level，跳过本次普通商店清理 dim={} pos=({},{},{})",
            chest.dimId,
            chest.pos.x,
            chest.pos.y,
            chest.pos.z
        );
        return true;
    }

    auto* dimPlayer = findOnlinePlayerInDimension(*level, chest.dimId);
    if (!dimPlayer) {
        logger.debug(
            "箱子过期检查：维度 {} 无在线玩家，无法确认实体库存，跳过本次普通商店清理 pos=({},{},{})",
            chest.dimId,
            chest.pos.x,
            chest.pos.y,
            chest.pos.z
        );
        return true;
    }

    auto& repo   = ShopRepository::getInstance();
    auto& region = dimPlayer->getDimensionBlockSource();

    // 一次容器遍历统计所有商品，避免每个商品各扫一遍整个箱子。
    // 箱子可读性是整箱属性，所以这里读不到就和原来一样整体跳过。
    std::vector<std::string> nbtList;
    nbtList.reserve(items.size());
    for (const auto& item : items) {
        nbtList.push_back(item.itemNbt);
    }

    auto realStocks = FormUtils::tryCountItemsInChestBatch(region, chest.pos, chest.dimId, nbtList);
    if (!realStocks) {
        logger.debug(
            "箱子过期检查：实体库存不可读，跳过本次普通商店清理 dim={} pos=({},{},{})",
            chest.dimId,
            chest.pos.x,
            chest.pos.y,
            chest.pos.z
        );
        return true;
    }

    for (size_t i = 0; i < items.size() && i < realStocks->size(); ++i) {
        const auto& item      = items[i];
        const int   realStock = (*realStocks)[i];

        if (realStock != item.dbCount && !repo.updateDbCount(chest.pos, chest.dimId, item.itemId, realStock)) {
            logger.warn(
                "箱子过期检查：库存同步失败，跳过本次普通商店清理 dim={} pos=({},{},{}) item={} realStock={}",
                chest.dimId,
                chest.pos.x,
                chest.pos.y,
                chest.pos.z,
                item.itemId,
                realStock
            );
            return true;
        }

        if (realStock > 0) {
            logger.info(
                "箱子过期检查：商店已有真实库存，跳过清理 dim={} pos=({},{},{}) item={} stock={}",
                chest.dimId,
                chest.pos.x,
                chest.pos.y,
                chest.pos.z,
                item.itemId,
                realStock
            );
            return true;
        }
    }

    return false;
}

void notifyOwnerOnline(Player& owner, ChestType type, const BlockPos& pos, int dimId, double refund) {
    auto&       txt     = TextService::getInstance();
    std::string typeStr = txt.getChestTypeName(type);

    std::map<std::string, std::string> params = {
        {"type", typeStr},
        {"dim",  dimIdToLocalizedString(dimId)},
        {"x",    std::to_string(pos.x)          },
        {"y",    std::to_string(pos.y)          },
        {"z",    std::to_string(pos.z)          }
    };

    if (refund > 0.0) {
        params["price"] = MoneyFormat::format(refund);
        owner.sendMessage(txt.getMessage("chest.expiry_notice_refund", params));
    } else {
        owner.sendMessage(txt.getMessage("chest.expiry_notice", params));
    }
}

} // namespace

ChestExpiryService& ChestExpiryService::getInstance() {
    static ChestExpiryService instance;
    return instance;
}

void ChestExpiryService::checkAndExpire() {
    const auto& expiryConfig = ConfigManager::getInstance().get().chestExpirySettings;
    if (!expiryConfig.enabled) {
        return;
    }

    int64_t shopExpirySeconds = 0;
    if (expiryConfig.shopExpiryDays > 0) {
        shopExpirySeconds = static_cast<int64_t>(expiryConfig.shopExpiryDays) * SECONDS_PER_DAY;
    }

    if (shopExpirySeconds <= 0) {
        return;
    }

    auto expiredChests = ChestRepository::getInstance().findExpiredChests(shopExpirySeconds);
    if (expiredChests.empty()) {
        return;
    }

    logger.info("箱子过期检查：发现 {} 个过期商店箱子，开始清理。", expiredChests.size());

    auto level    = ll::service::getLevel();
    auto levelPtr = level ? &*level : nullptr;
    auto& repo = ChestRepository::getInstance();
    auto& ftm  = FloatingTextManager::getInstance();

    int successCount    = 0;
    int refundCount     = 0;
    int refundFailCount = 0;

    for (const auto& chest : expiredChests) {
        ChestType type = chest.type;

        if (shouldKeepExpiredShopAfterInventoryCheck(chest, levelPtr)) {
            continue;
        }

        // 1. 从数据库删除记录（外键级联会清理 shop_items / recycle_shop_items / shared_chests 等）
        if (!repo.remove(chest.pos, chest.dimId)) {
            logger.error(
                "箱子过期清理失败：删除数据库记录失败 dim={} pos=({},{},{})",
                chest.dimId,
                chest.pos.x,
                chest.pos.y,
                chest.pos.z
            );
            continue;
        }

        // 2. 使箱子缓存和级联表查询缓存失效
        invalidateChestCacheAround(chest.pos, chest.dimId);
        clearExpiredChestRelatedQueryCaches();

        // 3. 移除悬浮字
        ftm.removeFloatingText(chest.pos, chest.dimId);

        // 4. 计算返还金额（若启用过期返还）
        double refund = 0.0;
        if (expiryConfig.refundOnExpiry && !chest.ownerUuid.empty()) {
            refund = getChestRemovalRefund(type);
            if (refund > 0.0) {
                if (Economy::addMoneyByUuid(chest.ownerUuid, refund)) {
                    ++refundCount;
                } else {
                    ++refundFailCount;
                    logger.warn(
                        "箱子过期返还失败：owner={} dim={} pos=({},{},{}) refund={}",
                        chest.ownerUuid,
                        chest.dimId,
                        chest.pos.x,
                        chest.pos.y,
                        chest.pos.z,
                        refund
                    );
                    // 返还失败时仍发送无返还金额的通知
                    refund = 0.0;
                }
            }
        }

        // 5. 通知在线的箱子主人
        if (levelPtr) {
            auto* playerPtr = findOnlinePlayerByUuidString(*levelPtr, chest.ownerUuid);
            if (playerPtr) {
                notifyOwnerOnline(*playerPtr, type, chest.pos, chest.dimId, refund);
            }
        }

        logger.info(
            "箱子已过期并清理：dim={} pos=({},{},{}) type={} owner={}",
            chest.dimId,
            chest.pos.x,
            chest.pos.y,
            chest.pos.z,
            static_cast<int>(type),
            chest.ownerUuid
        );
        ++successCount;
    }

    logger.info(
        "箱子过期检查完成：共清理 {} 个箱子，返还成功 {} 个，返还失败 {} 个。",
        successCount,
        refundCount,
        refundFailCount
    );
}

ll::coro::CoroTask<> ChestExpiryService::periodicCheckCoroutine() {
    logger.debug("ChestExpiryService: 周期性过期检查协程已启动。");

    while (!mShouldStop.load()) {
        try {
            checkAndExpire();
        } catch (const std::exception& e) {
            logger.error("ChestExpiryService: 过期检查异常: {}", e.what());
        } catch (...) {
            logger.error("ChestExpiryService: 过期检查发生未知异常。");
        }

        int intervalMinutes = ConfigManager::getInstance().get().chestExpirySettings.checkIntervalMinutes;
        if (intervalMinutes <= 0) {
            intervalMinutes = 60;
        }

        co_await ll::coro::SleepAwaiter(std::chrono::minutes(intervalMinutes));
    }

    logger.debug("ChestExpiryService: 周期性过期检查协程已停止。");
}

void ChestExpiryService::startPeriodicCheck() {
    if (mCheckTask.has_value()) {
        return; // 已经在运行
    }

    const auto& expiryConfig = ConfigManager::getInstance().get().chestExpirySettings;
    if (!expiryConfig.enabled) {
        return;
    }

    mShouldStop.store(false);
    mCheckTask.emplace(periodicCheckCoroutine());
    mCheckTask->launch(ll::thread::ServerThreadExecutor::getDefault());
    logger.info("箱子商店过期检查已启动，检查间隔: {} 分钟。", expiryConfig.checkIntervalMinutes);
}

void ChestExpiryService::stopPeriodicCheck() {
    mShouldStop.store(true);
    mCheckTask.reset();
}

} // namespace CT
