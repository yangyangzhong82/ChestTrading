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
    auto& i18n = I18nService::getInstance();

    try {
        logger.info(
            "[导入商店] 开始: path={}, pos=({},{},{}), dim={}, mode={}",
            filePath, pos.x, pos.y, pos.z, dimId, replaceExisting ? "replace" : "merge"
        );

        // 步骤1: 验证目标箱子
        logger.info("[导入商店] 步骤1: 验证目标箱子");
        auto& region       = player.getDimensionBlockSource();
        auto& chestService = ChestService::getInstance();
        auto  mainPos      = chestService.getMainChestPos(pos, region);
        auto  chestInfo    = chestService.getChestInfo(mainPos, dimId, region);
        if (!chestInfo || chestInfo->type != ChestType::AdminShop) {
            logger.warn("[导入商店] 目标箱子不是官方商店: pos=({},{},{}), dim={}", mainPos.x, mainPos.y, mainPos.z, dimId);
            return {false, 0, 0, i18n.get("command.import_shop_invalid_target")};
        }

        // 步骤2: 读取文件
        logger.info("[导入商店] 步骤2: 读取文件 path={}", filePath);
        std::u8string utf8Path;
        utf8Path.reserve(filePath.size());
        for (unsigned char ch : filePath) {
            utf8Path.push_back(static_cast<char8_t>(ch));
        }
        std::filesystem::path jsonPath(utf8Path);
        std::ifstream         file(jsonPath, std::ios::binary);
        if (!file.is_open()) {
            logger.warn("[导入商店] 无法打开文件: path={}", filePath);
            return {false, 0, 0, i18n.get("command.import_shop_file_not_found", {{"path", filePath}})};
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        logger.info("[导入商店] 文件读取完成, size={}", content.size());

        // 步骤3: 解析JSON
        logger.info("[导入商店] 步骤3: 解析JSON");
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(content, nullptr, true, true);
        } catch (const std::exception& e) {
            logger.error("[导入商店] JSON解析失败: path={}, error={}", filePath, e.what());
            return {false, 0, 0, i18n.get("command.import_shop_parse_fail", {{"error", e.what()}})};
        }

        auto purchaseItemsIt = root.find("purchaseItems");
        if (purchaseItemsIt == root.end() || !purchaseItemsIt->is_object()) {
            logger.warn("[导入商店] 配置中未找到purchaseItems对象: path={}", filePath);
            return {false, 0, 0, i18n.get("command.import_shop_no_items")};
        }
        logger.info("[导入商店] purchaseItems条目数: {}", purchaseItemsIt->size());

        // 步骤4: 解析商品条目
        logger.info("[导入商店] 步骤4: 解析商品条目");
        std::vector<ImportEntry> entries;
        int                      skippedCount = 0;
        entries.reserve(purchaseItemsIt->size());

        for (const auto& [key, rawValue] : purchaseItemsIt->items()) {
            if (!rawValue.is_object()) {
                ++skippedCount;
                logger.warn("[导入商店] 跳过: 条目不是对象, key={}", key);
                continue;
            }

            std::string itemNbt = getStringOrNull(rawValue, "itemData").value_or(key);
            if (itemNbt.empty()) {
                ++skippedCount;
                logger.warn("[导入商店] 跳过: 缺少itemData, key={}", key);
                continue;
            }

            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbt);
            if (!itemPtr) {
                ++skippedCount;
                logger.warn("[导入商店] 跳过: 无法解析itemData, key={}, nbt_len={}", key, itemNbt.size());
                continue;
            }

            auto priceOpt = getNumberOrNull(rawValue, "price");
            if (!priceOpt.has_value() || *priceOpt < 0.0) {
                ++skippedCount;
                logger.warn("[导入商店] 跳过: price无效, key={}", key);
                continue;
            }

            int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbt);
            if (itemId < 0) {
                ++skippedCount;
                logger.warn("[导入商店] 跳过: 无法创建item_id, key={}", key);
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

        logger.info("[导入商店] 解析完成: 有效={}, 跳过={}", entries.size(), skippedCount);

        if (entries.empty()) {
            return {false, 0, skippedCount, i18n.get("command.import_shop_no_valid_items", {{"skipped", std::to_string(skippedCount)}})};
        }

        // 步骤5: 数据库写入
        logger.info("[导入商店] 步骤5: 开始数据库写入");
        auto& db   = Sqlite3Wrapper::getInstance();
        auto& repo = ShopRepository::getInstance();
        Transaction txn(db);
        if (!txn.isActive()) {
            logger.error("[导入商店] 无法开始事务: path={}", filePath);
            return {false, 0, skippedCount, i18n.get("command.import_shop_failed_txn_begin")};
        }

        if (replaceExisting) {
            logger.info("[导入商店] 清空旧商品: pos=({},{},{}), dim={}", mainPos.x, mainPos.y, mainPos.z, dimId);
            if (!repo.removeAllItems(mainPos, dimId) || !DynamicPricingRepository::getInstance().removeAll(mainPos, dimId)) {
                txn.rollback();
                logger.error(
                    "[导入商店] 清空旧商品失败: pos=({},{},{}), dim={}",
                    mainPos.x, mainPos.y, mainPos.z, dimId
                );
                return {false, 0, skippedCount, i18n.get("command.import_shop_failed_clear")};
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
                    "[导入商店] 写入商品失败: {}/{}, itemId={}, pos=({},{},{}), dim={}",
                    importedCount + 1, entries.size(), entry.itemId,
                    mainPos.x, mainPos.y, mainPos.z, dimId
                );
                return {false, importedCount, skippedCount, i18n.get("command.import_shop_failed_write", {
                    {"imported", std::to_string(importedCount + 1)},
                    {"total", std::to_string(entries.size())},
                    {"item_id", std::to_string(entry.itemId)}
                })};
            }
            ++importedCount;
        }

        logger.info("[导入商店] 步骤6: 提交事务, 已写入{}个商品", importedCount);
        if (!txn.commit()) {
            logger.error("[导入商店] 事务提交失败: path={}, imported={}", filePath, importedCount);
            return {false, importedCount, skippedCount, i18n.get("command.import_shop_failed_commit", {
                {"imported", std::to_string(importedCount)}
            })};
        }

        logger.info("[导入商店] 步骤7: 更新浮空字");
        FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::AdminShop);

        logger.info("[导入商店] 完成: imported={}, skipped={}, path={}", importedCount, skippedCount, filePath);
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
    } catch (const std::exception& e) {
        logger.error("[导入商店] 未预期的异常: path={}, error={}", filePath, e.what());
        return {false, 0, 0, i18n.get("command.import_shop_parse_fail", {{"error", e.what()}})};
    } catch (...) {
        logger.error("[导入商店] 未知异常(可能是SEH): path={}", filePath);
        return {false, 0, 0, i18n.get("command.import_shop_failed_unknown")};
    }
}

} // namespace CT
