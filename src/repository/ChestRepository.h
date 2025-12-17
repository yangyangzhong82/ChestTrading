#pragma once

#include "Types.h"
#include "mc/world/level/BlockPos.h"
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
    int                      countByOwnerAndType(const std::string& ownerUuid, ChestType type);

    // === 分享管理 ===
    bool                         addSharedPlayer(const SharedChestData& data);
    bool                         removeSharedPlayer(const std::string& playerUuid, BlockPos pos, int dimId);
    std::vector<SharedChestData> getSharedPlayers(BlockPos pos, int dimId);
    bool                         isPlayerShared(const std::string& playerUuid, BlockPos pos, int dimId);

    // === 配置更新 ===
    bool updateConfig(BlockPos pos, int dimId, bool enableFloatingText, bool enableFakeItem, bool isPublic);
    bool updateShopName(BlockPos pos, int dimId, const std::string& shopName);

private:
    ChestRepository() = default;
};

} // namespace CT