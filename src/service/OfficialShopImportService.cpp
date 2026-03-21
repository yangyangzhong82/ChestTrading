#include "OfficialShopImportService.h"

#include "FloatingText/FloatingText.h"
#include "db/Sqlite3Wrapper.h"
#include "form/FormUtils.h"
#include "logger.h"
#include "nlohmann/json.hpp"
#include "repository/DynamicPricingRepository.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/I18nService.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace CT {

namespace {

struct ImportEntry {
    int         itemId;
    std::string itemNbt;
    double      price;
    int         dbCount;
};

std::optional<std::string> getStringOrNull(const nlohmann::json& value, const char* key) {
    auto it = value.find(key);
    if (it == value.end() || it->is_null() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<double> getNumberOrNull(const nlohmann::json& value, const char* key) {
    auto it = value.find(key);
    if (it == value.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number()) {
        return it->get<double>();
    }
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<long long> getIntegerOrNull(const nlohmann::json& value, const char* key) {
    auto it = value.find(key);
    if (it == value.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number_integer()) {
        return it->get<long long>();
    }
    if (it->is_number()) {
        return static_cast<long long>(it->get<double>());
    }
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

int makeVisibleDisplayCount(long long quantity) {
    // 官方商店购买逻辑无限库存，这里只保留一个正数让商品正常显示在列表中。
    long long normalized = quantity > 0 ? quantity : 1;
    return static_cast<int>(std::clamp<long long>(normalized, 1, std::numeric_limits<int>::max()));
}

} // namespace

OfficialShopImportService& OfficialShopImportService::getInstance() {
    static OfficialShopImportService instance;
    return instance;
}

OfficialShopImportResult
OfficialShopImportService::importPurchaseItems(Player& player, BlockPos pos, int dimId, const std::string& filePath, bool replaceExisting) {
    auto& i18n         = I18nService::getInstance();
    auto& region       = player.getDimensionBlockSource();
    auto& chestService = ChestService::getInstance();
    auto  mainPos      = chestService.getMainChestPos(pos, region);
    auto  chestInfo    = chestService.getChestInfo(mainPos, dimId, region);
    if (!chestInfo || chestInfo->type != ChestType::AdminShop) {
        return {false, 0, 0, i18n.get("command.import_shop_invalid_target")};
    }

    std::u8string         utf8Path;
    utf8Path.reserve(filePath.size());
    for (unsigned char ch : filePath) {
        utf8Path.push_back(static_cast<char8_t>(ch));
    }
    std::filesystem::path jsonPath(utf8Path);
    std::ifstream         file(jsonPath, std::ios::binary);
    if (!file.is_open()) {
        return {false, 0, 0, i18n.get("command.import_shop_file_not_found", {{"path", filePath}})};
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(content, nullptr, true, true);
    } catch (const std::exception& e) {
        logger.error("导入官方商店配置失败，JSON 解析错误: path={}, error={}", filePath, e.what());
        return {false, 0, 0, i18n.get("command.import_shop_parse_fail")};
    }

    auto purchaseItemsIt = root.find("purchaseItems");
    if (purchaseItemsIt == root.end() || !purchaseItemsIt->is_object()) {
        return {false, 0, 0, i18n.get("command.import_shop_no_items")};
    }

    std::vector<ImportEntry> entries;
    int                      skippedCount = 0;
    entries.reserve(purchaseItemsIt->size());

    for (const auto& [key, rawValue] : purchaseItemsIt->items()) {
        if (!rawValue.is_object()) {
            ++skippedCount;
            logger.warn("跳过导入商品：条目不是对象，path={}, key={}", filePath, key);
            continue;
        }

        std::string itemNbt = getStringOrNull(rawValue, "itemData").value_or(key);
        if (itemNbt.empty()) {
            ++skippedCount;
            logger.warn("跳过导入商品：缺少 itemData，path={}, key={}", filePath, key);
            continue;
        }

        auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbt);
        if (!itemPtr) {
            ++skippedCount;
            logger.warn("跳过导入商品：无法解析 itemData，path={}, key={}", filePath, key);
            continue;
        }

        auto priceOpt = getNumberOrNull(rawValue, "price");
        if (!priceOpt.has_value() || *priceOpt < 0.0) {
            ++skippedCount;
            logger.warn("跳过导入商品：price 无效，path={}, key={}", filePath, key);
            continue;
        }

        int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbt);
        if (itemId < 0) {
            ++skippedCount;
            logger.warn("跳过导入商品：无法创建 item_id，path={}, key={}", filePath, key);
            continue;
        }

        long long sourceQuantity = getIntegerOrNull(rawValue, "quantity").value_or(-1);
        entries.push_back(ImportEntry{
            .itemId  = itemId,
            .itemNbt = std::move(itemNbt),
            .price   = *priceOpt,
            .dbCount = makeVisibleDisplayCount(sourceQuantity)
        });
    }

    if (entries.empty()) {
        return {false, 0, skippedCount, i18n.get("command.import_shop_no_valid_items")};
    }

    auto& db   = Sqlite3Wrapper::getInstance();
    auto& repo = ShopRepository::getInstance();
    Transaction txn(db);
    if (!txn.isActive()) {
        logger.error("导入官方商店配置失败：无法开始事务，path={}", filePath);
        return {false, 0, skippedCount, i18n.get("command.import_shop_failed")};
    }

    if (replaceExisting) {
        if (!repo.removeAllItems(mainPos, dimId) || !DynamicPricingRepository::getInstance().removeAll(mainPos, dimId)) {
            txn.rollback();
            logger.error(
                "导入官方商店配置失败：清空旧商品失败，path={}, dim={}, pos=({}, {}, {})",
                filePath,
                dimId,
                mainPos.x,
                mainPos.y,
                mainPos.z
            );
            return {false, 0, skippedCount, i18n.get("command.import_shop_failed")};
        }
    }

    int importedCount = 0;
    for (const auto& entry : entries) {
        ShopItemData item{
            .dimId   = dimId,
            .pos     = mainPos,
            .itemId  = entry.itemId,
            .itemNbt = entry.itemNbt,
            .price   = entry.price,
            .dbCount = entry.dbCount,
            .slot    = 0
        };
        if (!repo.upsertItem(item)) {
            txn.rollback();
            logger.error(
                "导入官方商店配置失败：写入商品失败，path={}, itemId={}, dim={}, pos=({}, {}, {})",
                filePath,
                entry.itemId,
                dimId,
                mainPos.x,
                mainPos.y,
                mainPos.z
            );
            return {false, importedCount, skippedCount, i18n.get("command.import_shop_failed")};
        }
        ++importedCount;
    }

    if (!txn.commit()) {
        logger.error("导入官方商店配置失败：事务提交失败，path={}", filePath);
        return {false, importedCount, skippedCount, i18n.get("command.import_shop_failed")};
    }

    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::AdminShop);

    return {
        true,
        importedCount,
        skippedCount,
        i18n.get(
            replaceExisting ? "command.import_shop_success_replace" : "command.import_shop_success_merge",
            {
                {"imported", std::to_string(importedCount)},
                {"skipped",  std::to_string(skippedCount)},
                {"path",     filePath}
            }
        )
    };
}

} // namespace CT
