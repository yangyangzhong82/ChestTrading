#include "Utils/TradeRestrictionUtils.h"

#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "compat/PermissionCompat.h"
#include "mc/nbt/ByteTag.h"
#include "mc/world/item/ItemStack.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace CT::TradeRestrictionUtils {

namespace {

std::string trimCopy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    auto end =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool matchesBlockedItem(const std::string& itemTypeName, const std::vector<std::string>& blockedItems) {
    if (itemTypeName.empty()) {
        return false;
    }

    const std::string normalizedItemTypeName = normalizeItemTypeName(itemTypeName);
    for (const auto& blockedItem : blockedItems) {
        if (normalizeItemTypeName(blockedItem) == normalizedItemTypeName) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string normalizeItemTypeName(const std::string& itemTypeName) {
    std::string normalized = trimCopy(itemTypeName);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized.empty()) {
        return {};
    }
    if (normalized.find(':') == std::string::npos) {
        normalized = "minecraft:" + normalized;
    }
    return normalized;
}

std::string extractNormalizedItemTypeNameFromItemNbt(const std::string& itemNbt) {
    auto tag = NbtUtils::parseSNBT(itemNbt);
    if (!tag) {
        return {};
    }

    if (!tag->contains("Count")) {
        (*tag)["Count"] = ByteTag(1);
    }

    auto item = NbtUtils::createItemFromNbt(*tag);
    if (!item || item->isNull()) {
        return {};
    }

    return normalizeItemTypeName(item->getTypeName());
}

bool isItemBlockedForShop(const std::string& itemTypeName) {
    return matchesBlockedItem(itemTypeName, ConfigManager::getInstance().get().tradeRestrictionSettings.blockedShopItems);
}

bool isItemBlockedForRecycle(const std::string& itemTypeName) {
    return matchesBlockedItem(
        itemTypeName,
        ConfigManager::getInstance().get().tradeRestrictionSettings.blockedRecycleItems
    );
}

bool canBypassRestrictions(const std::string& playerUuid) {
    return !playerUuid.empty() && PermissionCompat::hasPermission(playerUuid, "chest.admin");
}

} // namespace CT::TradeRestrictionUtils
