#pragma once

#include "FloatingText/FloatingText.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

namespace ll::form {
class CustomForm;
}
class BlockSource;
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CT {

// 用于 GetAllChests 的返回结构
struct ChestInfo {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    ChestType   type;
    std::string shopName;
    bool        enableFloatingText = true;
    bool        enableFakeItem     = true;
    bool        isPublic           = true;
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
    // 使用 (dimId, x, y, z) 作为键
    struct PositionKey {
        int dimId;
        int x, y, z;

        bool operator==(const PositionKey& other) const {
            return dimId == other.dimId && x == other.x && y == other.y && z == other.z;
        }
    };

    struct PositionKeyHash {
        std::size_t operator()(const PositionKey& key) const {
            // 使用更好的哈希组合，避免冲突
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
    std::atomic<int>                                                  mCacheTimeoutSeconds{300}; // 默认缓存5分钟

    ChestCacheManager() = default;

public:
    static ChestCacheManager& getInstance() {
        static ChestCacheManager instance;
        return instance;
    }

    // 获取缓存的箱子信息
    bool getCachedChestInfo(BlockPos pos, int dimId, ChestCacheEntry& entry);

    // 设置缓存的箱子信息
    void setCachedChestInfo(BlockPos pos, int dimId, const ChestCacheEntry& entry);

    // 使缓存失效
    void invalidateCache(BlockPos pos, int dimId);

    // 清除所有缓存
    void clearAllCache();

    // 设置缓存超时时间（秒）
    void setCacheTimeout(int seconds);

    // 清理过期缓存
    void cleanupExpiredCache();
};

// 检查箱子状态，返回是否锁定、主人UUID和箱子类型
std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId, BlockSource& region);

// 设置箱子（上锁、设置商店等）
bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type);

// 移除箱子设置（解锁）
bool removeChest(BlockPos pos, int dimId, BlockSource& region);

// 添加分享玩家
bool addSharedPlayer(
    const std::string& owner_uuid,
    const std::string& shared_player_uuid,
    BlockPos           pos,
    int                dimId,
    BlockSource*       region = nullptr
);

// 移除分享玩家
bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region = nullptr);

// 获取所有分享玩家（包含主人信息）
std::vector<std::pair<std::string, std::string>>
getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region);

// 获取所有分享玩家（仅玩家UUID）
std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId, BlockSource& region);

// 检查玩家是否有权打开箱子
bool canPlayerOpenChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region);

namespace internal {
// 获取箱子的主方块位置，对于双联箱，这总是一个确定的方块
BlockPos GetMainChestPos(BlockPos pos, BlockSource& region);
} // namespace internal

// 获取所有箱子信息
std::vector<ChestInfo> getAllChests();

// 获取商店名称
std::string getShopName(BlockPos pos, int dimId, BlockSource& region);

// 设置商店名称
bool setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& shopName);

// 获取箱子配置
struct ChestConfig {
    bool enableFloatingText = true;
    bool enableFakeItem     = true;
    bool isPublic           = true;
};
ChestConfig getChestConfig(BlockPos pos, int dimId, BlockSource& region);

// 设置箱子配置
bool setChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfig& config);

// 获取玩家拥有的特定类型箱子数量
int getPlayerChestCount(const std::string& playerUuid, ChestType type);

// 检查玩家是否可以创建特定类型的箱子（检查数量限制和权限）
bool canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage);

} // namespace CT
