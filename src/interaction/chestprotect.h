#pragma once

#include "FloatingText/FloatingText.h" // 引入 FloatingTextManager
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

namespace ll::form {
class CustomForm;
}
class BlockSource;
#include <string>
#include <tuple>
#include <utility> // For std::pair
#include <vector>  // For std::vector
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace CT {

// 用于 GetAllChests 的返回结构
struct ChestInfo {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    ChestType   type;
};


// 箱子信息缓存结构
struct ChestCacheEntry {
    bool isLocked;
    std::string ownerUuid;
    ChestType chestType;
    std::chrono::steady_clock::time_point timestamp;
    
    ChestCacheEntry() : isLocked(false), chestType(ChestType::Invalid) {}
    ChestCacheEntry(bool locked, std::string uuid, ChestType type)
        : isLocked(locked), ownerUuid(std::move(uuid)), chestType(type),
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
            // 使用简单的哈希组合
            std::size_t h1 = std::hash<int>{}(key.dimId);
            std::size_t h2 = std::hash<int>{}(key.x);
            std::size_t h3 = std::hash<int>{}(key.y);
            std::size_t h4 = std::hash<int>{}(key.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };
    
    std::unordered_map<PositionKey, ChestCacheEntry, PositionKeyHash> mCache;
    mutable std::mutex mCacheMutex;
    int mCacheTimeoutSeconds = 300; // 默认缓存5分钟
    
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
bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region = nullptr);

// 移除分享玩家
bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region = nullptr);

// 获取所有分享玩家（包含主人信息）
std::vector<std::pair<std::string, std::string>> getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region);

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


} // namespace CT
