#include "ChestPackService.h"

#include "FloatingText/FloatingText.h"
#include "Utils/NbtUtils.h"
#include "compat/PLandCompat.h"
#include "compat/PermissionCompat.h"
#include "mc/nbt/CompoundTagVariant.h"
#include "mc/nbt/ListTag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ChestRepository.h"
#include "service/ChestService.h"
#include "service/TextService.h"

#include <optional>
#include <vector>

namespace CT {

namespace {

std::string escapeSnbtString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

std::unique_ptr<CompoundTag> buildPackedChestDisplayTag(TextService& txt, const std::optional<ChestData>& chestInfo) {
    std::vector<std::string> loreLines{
        txt.getMessage("chest.pack_item_lore")
    };

    if (chestInfo.has_value()) {
        loreLines.push_back(txt.getMessage(
            "chest.pack_item_type_lore",
            {
                {"type", txt.getChestTypeName(chestInfo->type)}
            }
        ));
    }

    std::string snbt = "{Lore:[";
    for (size_t i = 0; i < loreLines.size(); ++i) {
        if (i > 0) {
            snbt += ",";
        }
        snbt += "\"" + escapeSnbtString(loreLines[i]) + "\"";
    }
    snbt += "]}";

    return NbtUtils::parseSNBT(snbt);
}

} // namespace

bool packChestForPlayer(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    auto        playerUuid   = player.getUuid().asString();
    auto&       txt          = TextService::getInstance();
    auto&       chestService = ChestService::getInstance();
    auto        mainPos      = chestService.getMainChestPos(pos, region);
    auto*       blockActor   = region.getBlockEntity(mainPos);

    if (!PermissionCompat::hasPermission(playerUuid, "chest.pack")) {
        player.sendMessage(txt.getMessage("command.no_permission"));
        return false;
    }

    // Packing removes the original chest block, so destroy permission is required.
    if (!PLandCompat::getInstance().canDestroy(player, mainPos)) {
        player.sendMessage(txt.getMessage("chest.land_no_permission_pack"));
        return false;
    }

    // 大箱子不允许打包，避免破坏双箱结构。
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            player.sendMessage(txt.getMessage("chest.pack_large_chest_forbidden"));
            return false;
        }
    }

    auto chestInfo = chestService.getChestInfo(mainPos, dimId, region);
    if (chestInfo.has_value() && chestInfo->ownerUuid != playerUuid) {
        player.sendMessage(txt.getMessage("chest.pack_not_owner"));
        return false;
    }

    if (!blockActor) {
        player.sendMessage(txt.getMessage("chest.pack_entity_fail"));
        return false;
    }

    auto chestNbt = NbtUtils::getBlockEntityNbt(blockActor);
    if (!chestNbt || !chestNbt->contains("Items")) {
        player.sendMessage(txt.getMessage("chest.pack_empty"));
        return false;
    }

    CompoundTag itemNbt;
    itemNbt["Name"]        = StringTag(region.getBlock(mainPos).getTypeName());
    itemNbt["Count"]       = ByteTag(1);
    itemNbt["Damage"]      = ShortTag(0);
    itemNbt["WasPickedUp"] = ByteTag(0);

    CompoundTag tagNbt;
    tagNbt["Items"] = chestNbt->at("Items").get<ListTag>();
    if (auto displayTag = buildPackedChestDisplayTag(txt, chestInfo)) {
        tagNbt["display"] = *displayTag;
    }

    if (chestInfo.has_value()) {
        int64_t packedId = ChestRepository::getInstance().packChest(mainPos, dimId);
        if (packedId < 0) {
            player.sendMessage(txt.getMessage("chest.pack_config_fail"));
            return false;
        }

        CompoundTag ctData;
        ctData["packedId"]     = Int64Tag(packedId);
        tagNbt["ChestTrading"] = std::move(ctData);
    }

    itemNbt["tag"] = std::move(tagNbt);

    auto chestItem = NbtUtils::createItemFromNbt(itemNbt);
    if (!chestItem) {
        player.sendMessage(txt.getMessage("chest.pack_item_fail"));
        return false;
    }

    if (!player.addAndRefresh(*chestItem)) {
        player.sendMessage(txt.getMessage("chest.pack_inventory_full"));
        return false;
    }

    auto* chestActor = static_cast<ChestBlockActor*>(blockActor);
    chestActor->clearInventory(0);

    region.removeBlock(mainPos, BlockChangeContext{});

    if (chestInfo.has_value()) {
        ChestCacheManager::getInstance().invalidateCache(mainPos, dimId);
        FloatingTextManager::getInstance().removeFloatingText(mainPos, dimId);
    }

    player.sendMessage(txt.getMessage("chest.pack_success"));
    return true;
}

} // namespace CT
