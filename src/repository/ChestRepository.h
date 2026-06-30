#pragma once

#include "Types.h"
#include "mc/world/level/BlockPos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CT {

// 箱子数据结构
struct ChestData {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    ChestType   type;
    std::string shopName;
    bool        enableFloatingText = true;
    bool        enableFakeItem     = true;
    bool        isPublic           = true;
};

// 分享数据结构
struct SharedChestData {
    std::string playerUuid;
    std::string ownerUuid;
    int         dimId;
    BlockPos    pos;
};

/**
 * @brief 箱子数据访问层
 * 负责所有箱子相关的数据库操作，不包含业务逻辑
 */
class ChestRepository {
public:
    static ChestRepository& getInstance();

    // 禁止拷贝
    ChestRepository(const ChestRepository&)            = delete;
    ChestRepository& operator=(const ChestRepository&) = delete;

    // === 箱子CRUD ===
    bool                     insert(const ChestData& chest);
    bool                     update(const ChestData& chest);
    bool                     remove(BlockPos pos, int dimId);
    std::optional<ChestData> findByPosition(BlockPos pos, int dimId);
    std::vector<ChestData>   findByOwner(const std::string& ownerUuid);
    std::vector<ChestData>   findAll();
    std::vector<ChestData>   findAllPublicShops();
    int                      countByOwnerAndType(const std::string& ownerUuid, ChestType type);

    // === 过期检查 ===
    // 查询所有已过期的玩家商店/回收商店（官方商店永远不会过期，不会被返回）。
    // 过期条件：箱子中没有有效商品/委托，且超过 shopExpirySeconds 未补货。
    // shopExpirySeconds: 玩家商店/回收商店的过期秒数，<=0 表示不检查
    std::vector<ChestData>   findExpiredChests(int64_t shopExpirySeconds);

    // === 补货时间管理 ===
    // 刷新指定箱子最后一次补货/管理时间为当前时间（重置过期计时）。
    bool                     touchRestockTime(BlockPos pos, int dimId);

    // === 分享管理 ===
    bool                         addSharedPlayer(const SharedChestData& data);
    bool                         removeSharedPlayer(const std::string& playerUuid, BlockPos pos, int dimId);
    std::vector<SharedChestData> getSharedPlayers(BlockPos pos, int dimId);
    bool                         isPlayerShared(const std::string& playerUuid, BlockPos pos, int dimId);

    // === 配置更新 ===
    bool updateConfig(BlockPos pos, int dimId, bool enableFloatingText, bool enableFakeItem, bool isPublic);
    bool updateShopName(BlockPos pos, int dimId, const std::string& shopName);

    // === 打包/恢复 ===
    int64_t packChest(BlockPos pos, int dimId);                           // 返回 packed_id，失败返回 -1
    bool    unpackChest(int64_t packedId, BlockPos newPos, int newDimId); // 恢复到新位置

private:
    ChestRepository() = default;
};

} // namespace CT