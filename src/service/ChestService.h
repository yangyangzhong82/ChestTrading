#pragma once

#include "Types.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "repository/ChestRepository.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace CT {

// 操作结果
struct ChestOperationResult {
    bool                     success;
    std::string              message;
    std::optional<ChestData> data;
};

// 箱子配置
struct ChestConfigData {
    bool enableFloatingText = true;
    bool enableFakeItem     = true;
    bool isPublic           = true;
};

// 箱子信息缓存结构
struct ChestCacheEntry {
    bool                                  isLocked;
    std::string                           ownerUuid;
    ChestType                             chestType;
    std::chrono::steady_clock::time_point timestamp;

    ChestCacheEntry() : isLocked(false), chestType(ChestType::Invalid) {}
    ChestCacheEntry(bool locked, std::string uuid, ChestType type)
    : isLocked(locked),
      ownerUuid(std::move(uuid)),
      chestType(type),
      timestamp(std::chrono::steady_clock::now()) {}
};

// 箱子缓存管理器
class ChestCacheManager {
private:
    struct PositionKey {
        int  dimId;
        int  x, y, z;
        bool operator==(const PositionKey& other) const {
            return dimId == other.dimId && x == other.x && y == other.y && z == other.z;
        }
    };

    struct PositionKeyHash {
        std::size_t operator()(const PositionKey& key) const {
            std::size_t seed  = 0;
            seed             ^= std::hash<int>{}(key.dimId) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed             ^= std::hash<int>{}(key.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed             ^= std::hash<int>{}(key.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed             ^= std::hash<int>{}(key.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    std::unordered_map<PositionKey, ChestCacheEntry, PositionKeyHash> mCache;
    mutable std::mutex                                                mCacheMutex;
    std::atomic<int>                                                  mCacheTimeoutSeconds{300};

    ChestCacheManager() = default;

public:
    static ChestCacheManager& getInstance() {
        static ChestCacheManager instance;
        return instance;
    }

    bool getCachedChestInfo(BlockPos pos, int dimId, ChestCacheEntry& entry);
    void setCachedChestInfo(BlockPos pos, int dimId, const ChestCacheEntry& entry);
    void invalidateCache(BlockPos pos, int dimId);
    void clearAllCache();
    void setCacheTimeout(int seconds);
    void cleanupExpiredCache();
};

/**
 * @brief 箱子核心业务服务
 * 处理箱子的创建、删除、权限检查等业务逻辑
 */
class ChestService {
public:
    static ChestService& getInstance();

    ChestService(const ChestService&)            = delete;
    ChestService& operator=(const ChestService&) = delete;

    // === 核心操作 ===
    ChestOperationResult
    createChest(const std::string& playerUuid, BlockPos pos, int dimId, ChestType type, BlockSource& region);

    ChestOperationResult removeChest(BlockPos pos, int dimId, BlockSource& region);

    // === 查询 ===
    std::optional<ChestData> getChestInfo(BlockPos pos, int dimId, BlockSource& region);
    bool                     isChestLocked(BlockPos pos, int dimId, BlockSource& region);
    bool                     isOwner(const std::string& playerUuid, BlockPos pos, int dimId, BlockSource& region);

    // === 权限检查 ===
    bool canPlayerAccess(const std::string& playerUuid, BlockPos pos, int dimId, BlockSource& region);
    bool canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage);

    // === 分享管理 ===
    bool addSharedPlayer(
        const std::string& ownerUuid,
        const std::string& targetUuid,
        BlockPos           pos,
        int                dimId,
        BlockSource&       region
    );
    bool removeSharedPlayer(const std::string& targetUuid, BlockPos pos, int dimId, BlockSource& region);
    std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId, BlockSource& region);

    // === 配置管理 ===
    bool            updateChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfigData& config);
    ChestConfigData getChestConfig(BlockPos pos, int dimId, BlockSource& region);
    bool            setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& name);
    std::string     getShopName(BlockPos pos, int dimId, BlockSource& region);

    // === 工具方法 ===
    BlockPos getMainChestPos(BlockPos pos, BlockSource& region);
    int      getPlayerChestCount(const std::string& playerUuid, ChestType type);

    // === 公开商店列表 ===
    std::vector<ChestData> getAllPublicChests();

private:
    ChestService() = default;

    // 生成悬浮字文本
    std::string generateFloatingText(ChestType type, const std::string& ownerName);

    // 更新悬浮字显示
    void updateFloatingText(BlockPos pos, int dimId, const std::string& ownerUuid, ChestType type);
};

} // namespace CT