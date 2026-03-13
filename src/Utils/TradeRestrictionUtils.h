#pragma once

#include <string>

namespace CT::TradeRestrictionUtils {

std::string normalizeItemTypeName(const std::string& itemTypeName);

std::string extractNormalizedItemTypeNameFromItemNbt(const std::string& itemNbt);

bool isItemBlockedForShop(const std::string& itemTypeName);

bool isItemBlockedForRecycle(const std::string& itemTypeName);

bool canBypassRestrictions(const std::string& playerUuid);

} // namespace CT::TradeRestrictionUtils
