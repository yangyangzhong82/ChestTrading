// chestprotect.cpp - 向后兼容层，委托给新的服务层
#include "interaction/chestprotect.h"
#include "logger.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ChestRepository.h"
#include "service/ChestService.h"


namespace CT {

// ========== 向后兼容函数 - 委托给 ChestService ==========

std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId, BlockSource& region) {
    auto info = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (!info) {
        return {false, "", ChestType::Locked};
    }
    return {true, info->ownerUuid, info->type};
}

bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type) {
    auto result = ChestService::getInstance().createChest(player_uuid, pos, dimId, type, region);
    return result.success;
}

bool removeChest(BlockPos pos, int dimId, BlockSource& region) {
    auto result = ChestService::getInstance().removeChest(pos, dimId, region);
    return result.success;
}

bool addSharedPlayer(
    const std::string& owner_uuid,
    const std::string& shared_player_uuid,
    BlockPos           pos,
    int                dimId,
    BlockSource*       region
) {
    if (!region) return false;
    return ChestService::getInstance().addSharedPlayer(owner_uuid, shared_player_uuid, pos, dimId, *region);
}

bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region) {
    if (!region) return false;
    return ChestService::getInstance().removeSharedPlayer(shared_player_uuid, pos, dimId, *region);
}

std::vector<std::pair<std::string, std::string>>
getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = internal::GetMainChestPos(pos, region);
    auto     shared  = ChestRepository::getInstance().getSharedPlayers(mainPos, dimId);
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& s : shared) {
        result.push_back({s.playerUuid, s.ownerUuid});
    }
    return result;
}

std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId, BlockSource& region) {
    return ChestService::getInstance().getSharedPlayers(pos, dimId, region);
}

bool canPlayerOpenChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region) {
    return ChestService::getInstance().canPlayerAccess(player_uuid, pos, dimId, region);
}

namespace internal {
BlockPos GetMainChestPos(BlockPos pos, BlockSource& region) {
    // 直接实现，避免循环依赖
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return pos;
    }
    auto* chest = static_cast<ChestBlockActor*>(blockActor);
    if (!chest->mLargeChestPaired) {
        return pos;
    }
    BlockPos pairedPos = chest->mLargeChestPairedPosition;
    if (pos.x < pairedPos.x || (pos.x == pairedPos.x && pos.z < pairedPos.z)) {
        return pos;
    }
    return pairedPos;
}
} // namespace internal

std::vector<ChestInfo> getAllChests() {
    auto                   data = ChestRepository::getInstance().findAll();
    std::vector<ChestInfo> result;
    for (const auto& d : data) {
        ChestInfo info;
        info.dimId              = d.dimId;
        info.pos                = d.pos;
        info.ownerUuid          = d.ownerUuid;
        info.type               = d.type;
        info.shopName           = d.shopName;
        info.enableFloatingText = d.enableFloatingText;
        info.enableFakeItem     = d.enableFakeItem;
        info.isPublic           = d.isPublic;
        result.push_back(info);
    }
    return result;
}

std::string getShopName(BlockPos pos, int dimId, BlockSource& region) {
    return ChestService::getInstance().getShopName(pos, dimId, region);
}

bool setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& shopName) {
    return ChestService::getInstance().setShopName(pos, dimId, region, shopName);
}

ChestConfig getChestConfig(BlockPos pos, int dimId, BlockSource& region) {
    auto        cfg = ChestService::getInstance().getChestConfig(pos, dimId, region);
    ChestConfig result;
    result.enableFloatingText = cfg.enableFloatingText;
    result.enableFakeItem     = cfg.enableFakeItem;
    result.isPublic           = cfg.isPublic;
    return result;
}

bool setChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfig& config) {
    ChestConfigData cfg;
    cfg.enableFloatingText = config.enableFloatingText;
    cfg.enableFakeItem     = config.enableFakeItem;
    cfg.isPublic           = config.isPublic;
    return ChestService::getInstance().updateChestConfig(pos, dimId, region, cfg);
}

int getPlayerChestCount(const std::string& playerUuid, ChestType type) {
    return ChestService::getInstance().getPlayerChestCount(playerUuid, type);
}

bool canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage) {
    return ChestService::getInstance().canPlayerCreateChest(playerUuid, type, errorMessage);
}

} // namespace CT
