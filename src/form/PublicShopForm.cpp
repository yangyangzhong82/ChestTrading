#include "PublicShopForm.h"
#include "ShopForm.h"
#include "RecycleForm.h"
#include "FormUtils.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/chestprotect.h"
#include "FloatingText/FloatingText.h"
#include "Utils/NbtUtils.h"
#include "Utils/MoneyFormat.h"
#include "logger.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include <algorithm>
#include <cctype>


namespace CT {

static const int SHOPS_PER_PAGE = 10;

static std::string dimIdToString(int dimId) {
    switch (dimId) {
    case 0: return "主世界";
    case 1: return "下界";
    case 2: return "末地";
    default: return "未知维度";
    }
}

// 转换为小写用于模糊搜索
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

// 模糊匹配
static bool fuzzyMatch(const std::string& text, const std::string& keyword) {
    return toLower(text).find(toLower(keyword)) != std::string::npos;
}

// 检查商店是否包含匹配的物品（从数据库查询）
static bool shopContainsItem(const ChestInfo& shop, const std::string& keyword) {
    logger.debug("shopContainsItem: 搜索商店 ({},{},{}) dim={} 关键词: {}", shop.pos.x, shop.pos.y, shop.pos.z, shop.dimId, keyword);
    
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> itemResults;
    
    if (shop.type == ChestType::Shop) {
        itemResults = db.query(
            "SELECT id.item_nbt FROM shop_items si JOIN item_definitions id ON si.item_id = id.item_id WHERE si.dim_id = ? AND si.pos_x = ? AND si.pos_y = ? AND si.pos_z = ?;",
            shop.dimId, shop.pos.x, shop.pos.y, shop.pos.z
        );
    } else if (shop.type == ChestType::RecycleShop) {
        itemResults = db.query(
            "SELECT id.item_nbt FROM recycle_shop_items rsi JOIN item_definitions id ON rsi.item_id = id.item_id WHERE rsi.dim_id = ? AND rsi.pos_x = ? AND rsi.pos_y = ? AND rsi.pos_z = ?;",
            shop.dimId, shop.pos.x, shop.pos.y, shop.pos.z
        );
    }
    
    logger.debug("shopContainsItem: 查询到 {} 个物品", itemResults.size());
    
    for (const auto& row : itemResults) {
        if (!row.empty()) {
            logger.debug("shopContainsItem: 处理物品 NBT: {}", row[0].substr(0, 100));
            auto nbt = CT::NbtUtils::parseSNBT(row[0]);
            if (nbt) {
                (*nbt)["Count"] = ByteTag(1);
                auto itemPtr = CT::NbtUtils::createItemFromNbt(*nbt);
                if (itemPtr && !itemPtr->isNull()) {
                    std::string itemName = itemPtr->getName();
                    std::string typeName = itemPtr->getTypeName();
                    if (itemName.empty()) itemName = typeName;
                    logger.debug("shopContainsItem: 物品名称: {}, 类型: {}", itemName, typeName);
                    if (fuzzyMatch(itemName, keyword)) {
                        logger.debug("shopContainsItem: 匹配成功 (名称)");
                        return true;
                    }
                    if (fuzzyMatch(typeName, keyword)) {
                        logger.debug("shopContainsItem: 匹配成功 (类型)");
                        return true;
                    }
                } else {
                    logger.warn("shopContainsItem: 无法从 NBT 创建物品");
                }
            } else {
                logger.warn("shopContainsItem: 无法解析 NBT: {}", row[0].substr(0, 100));
            }
        }
    }
    logger.debug("shopContainsItem: 未找到匹配物品");
    return false;
}

static void showSearchForm(Player& player, bool isRecycle) {
    ll::form::CustomForm fm;
    fm.setTitle(isRecycle ? "搜索回收商店" : "搜索商店");
    fm.appendDropdown("type", "搜索类型", {"店主名称", "物品名称/类型"}, 0);
    fm.appendInput("keyword", "关键词", "输入搜索关键词");
    fm.sendTo(player, [isRecycle](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) return;
        
        uint64 typeIdx = 0;
        std::string keyword;
        
        auto typeIt = result->find("type");
        if (typeIt != result->end()) {
            if (auto* ptr = std::get_if<uint64>(&typeIt->second)) {
                typeIdx = *ptr;
            } else if (auto* dptr = std::get_if<double>(&typeIt->second)) {
                typeIdx = static_cast<uint64>(*dptr);
            } else if (auto* sptr = std::get_if<std::string>(&typeIt->second)) {
                // dropdown 返回字符串时，检查是否是"物品名称/类型"
                if (*sptr == "物品名称/类型") {
                    typeIdx = 1;
                }
                logger.debug("showSearchForm: type 是 string, 值={}", *sptr);
            }
        }
        
        auto keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            logger.debug("showSearchForm: 找到 keyword 字段, variant index={}", keywordIt->second.index());
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
                logger.debug("showSearchForm: keyword={}", keyword);
            }
        } else {
            logger.warn("showSearchForm: 未找到 keyword 字段");
        }
        
        std::string searchType = (typeIdx == 0) ? "owner" : "item";
        logger.debug("showSearchForm: 搜索类型={}, 关键词={}", searchType, keyword);
        if (isRecycle) {
            showPublicRecycleShopListForm(p, 0, keyword, searchType);
        } else {
            showPublicShopListForm(p, 0, keyword, searchType);
        }
    });
}

void showPublicShopListForm(Player& player, int currentPage, const std::string& searchKeyword, const std::string& searchType) {
    logger.debug("showPublicShopListForm: 开始搜索, 关键词={}, 类型={}", searchKeyword, searchType);
    
    ll::form::SimpleForm fm;
    fm.setTitle("公开商店列表");

    auto allChests = getAllChests();
    logger.debug("showPublicShopListForm: 获取到 {} 个箱子", allChests.size());
    
    std::vector<ChestInfo> shops;
    for (const auto& chest : allChests) {
        // 只显示公开的商店
        if (chest.type == ChestType::Shop && chest.isPublic) {
            if (!searchKeyword.empty()) {
                if (searchType == "owner") {
                    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(chest.ownerUuid));
                    std::string ownerName = ownerInfo ? ownerInfo->name : "";
                    logger.debug("showPublicShopListForm: 检查店主 '{}' 是否匹配 '{}'", ownerName, searchKeyword);
                    if (!fuzzyMatch(ownerName, searchKeyword)) continue;
                } else if (searchType == "item") {
                    if (!shopContainsItem(chest, searchKeyword)) continue;
                }
            }
            shops.push_back(chest);
        }
    }
    logger.debug("showPublicShopListForm: 过滤后剩余 {} 个商店", shops.size());

    int totalShops = shops.size();
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (shops.empty()) {
        fm.setContent(searchKeyword.empty() ? "§7当前没有公开的商店。" : "§7没有找到匹配的商店。");
    } else {
        std::string contentText = "§e共 " + std::to_string(totalShops) + " 个商店 §7(第 " + 
                      std::to_string(currentPage + 1) + "/" + std::to_string(totalPages) + " 页)";
        if (!searchKeyword.empty()) {
            std::string typeStr = (searchType == "owner") ? "店主" : "物品";
            contentText += "\n§6搜索" + typeStr + ": " + searchKeyword;
        }
        fm.setContent(contentText);

        int startIdx = currentPage * SHOPS_PER_PAGE;
        int endIdx = std::min(startIdx + SHOPS_PER_PAGE, totalShops);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& shop = shops[i];
            auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
            std::string ownerName = ownerInfo ? ownerInfo->name : "未知";

            std::string shopDisplayName = shop.shopName.empty() ? (ownerName + " 的商店") : shop.shopName;
            std::string buttonText = ownerName + "§b" + shopDisplayName + "§r\n§7" + dimIdToString(shop.dimId) + " §e["
                                   + std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", "
                                   + std::to_string(shop.pos.z) + "]";

            fm.appendButton(buttonText, [shop](Player& p) {
                showShopPreviewForm(p, shop);
            });
        }
    }

    fm.appendButton("§e🔍 搜索", [](Player& p) {
        showSearchForm(p, false);
    });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton("§a<< 上一页", [currentPage, searchKeyword, searchType](Player& p) {
                showPublicShopListForm(p, currentPage - 1, searchKeyword, searchType);
            });
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton("§a下一页 >>", [currentPage, searchKeyword, searchType](Player& p) {
                showPublicShopListForm(p, currentPage + 1, searchKeyword, searchType);
            });
        }
    }

    fm.appendButton("§c关闭", [](Player& p) {});
    fm.sendTo(player);
}

void showPublicRecycleShopListForm(Player& player, int currentPage, const std::string& searchKeyword, const std::string& searchType) {
    ll::form::SimpleForm fm;
    fm.setTitle("公开回收商店列表");

    auto allChests = getAllChests();
    std::vector<ChestInfo> recycleShops;
    for (const auto& chest : allChests) {
        // 只显示公开的回收商店
        if (chest.type == ChestType::RecycleShop && chest.isPublic) {
            if (!searchKeyword.empty()) {
                if (searchType == "owner") {
                    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(chest.ownerUuid));
                    std::string ownerName = ownerInfo ? ownerInfo->name : "";
                    if (!fuzzyMatch(ownerName, searchKeyword)) continue;
                } else if (searchType == "item") {
                    if (!shopContainsItem(chest, searchKeyword)) continue;
                }
            }
            recycleShops.push_back(chest);
        }
    }

    int totalShops = recycleShops.size();
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (recycleShops.empty()) {
        fm.setContent(searchKeyword.empty() ? "§7当前没有公开的回收商店。" : "§7没有找到匹配的回收商店。");
    } else {
        std::string contentText = "§e共 " + std::to_string(totalShops) + " 个回收商店 §7(第 " + 
                      std::to_string(currentPage + 1) + "/" + std::to_string(totalPages) + " 页)";
        if (!searchKeyword.empty()) {
            std::string typeStr = (searchType == "owner") ? "店主" : "物品";
            contentText += "\n§6搜索" + typeStr + ": " + searchKeyword;
        }
        fm.setContent(contentText);

        int startIdx = currentPage * SHOPS_PER_PAGE;
        int endIdx = std::min(startIdx + SHOPS_PER_PAGE, totalShops);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& shop = recycleShops[i];
            auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
            std::string ownerName = ownerInfo ? ownerInfo->name : "未知";

            std::string shopDisplayName = shop.shopName.empty() ? (ownerName + " 的回收商店") : shop.shopName;
            std::string buttonText = "§b" + shopDisplayName + "§r\n§7" + 
                                     dimIdToString(shop.dimId) + " §e[" + 
                                     std::to_string(shop.pos.x) + ", " + 
                                     std::to_string(shop.pos.y) + ", " + 
                                     std::to_string(shop.pos.z) + "]";

            fm.appendButton(buttonText, [shop](Player& p) {
                showRecycleShopPreviewForm(p, shop);
            });
        }
    }

    fm.appendButton("§e🔍 搜索", [](Player& p) {
        showSearchForm(p, true);
    });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton("§a<< 上一页", [currentPage, searchKeyword, searchType](Player& p) {
                showPublicRecycleShopListForm(p, currentPage - 1, searchKeyword, searchType);
            });
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton("§a下一页 >>", [currentPage, searchKeyword, searchType](Player& p) {
                showPublicRecycleShopListForm(p, currentPage + 1, searchKeyword, searchType);
            });
        }
    }

    fm.appendButton("§c关闭", [](Player& p) {});
    fm.sendTo(player);
}

// 商店预览表单（只能预览物品，不能购买，可传送到箱子位置）
void showShopPreviewForm(Player& player, const ChestInfo& shop) {
    ll::form::SimpleForm fm;
    
    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : "未知";
    std::string shopDisplayName = shop.shopName.empty() ? (ownerName + " 的商店") : shop.shopName;
    
    fm.setTitle("商店预览 - " + shopDisplayName);

    auto& db = Sqlite3Wrapper::getInstance();
    auto results = db.query(
        "SELECT s.price, s.db_count, d.item_nbt FROM shop_items s "
        "JOIN item_definitions d ON s.item_id = d.item_id "
        "WHERE s.dim_id = ? AND s.pos_x = ? AND s.pos_y = ? AND s.pos_z = ?",
        shop.dimId, shop.pos.x, shop.pos.y, shop.pos.z
    );

    std::string content = "§e店主: " + ownerName + "\n";
    content += "§7位置: " + dimIdToString(shop.dimId) + " [" + 
               std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", " + 
               std::to_string(shop.pos.z) + "]\n\n";
    
    if (results.empty()) {
        content += "§7该商店暂无商品。\n";
    } else {
        content += "§a商品列表:\n";
        for (const auto& row : results) {
            double price = std::stod(row[0]);
            int dbCount = std::stoi(row[1]);
            std::string itemNbtStr = row[2];
            
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (itemPtr) {
                content += "§f- " + std::string(itemPtr->getName()) + " §7x" + std::to_string(dbCount) + 
                          " §6[" + CT::MoneyFormat::format(price) + "]\n";
            }
        }
    }
    content += "\n§c注意: 需要前往商店位置点击箱子才能购买！";
    fm.setContent(content);

    fm.appendButton("§a传送到商店", [shop](Player& p) {
        p.teleport({(float)shop.pos.x + 0.5f, (float)shop.pos.y + 1.0f, (float)shop.pos.z + 0.5f}, shop.dimId);
        p.sendMessage("§a已传送到商店位置，请点击箱子进行购买。");
    });

    fm.appendButton("§7返回列表", [](Player& p) {
        showPublicShopListForm(p);
    });

    fm.appendButton("§c关闭", [](Player& p) {});
    fm.sendTo(player);
}

// 回收商店预览表单（只能预览物品，不能回收，可传送到箱子位置）
void showRecycleShopPreviewForm(Player& player, const ChestInfo& shop) {
    ll::form::SimpleForm fm;
    
    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : "未知";
    std::string shopDisplayName = shop.shopName.empty() ? (ownerName + " 的回收商店") : shop.shopName;
    
    fm.setTitle("回收商店预览 - " + shopDisplayName);

    auto& db = Sqlite3Wrapper::getInstance();
    auto results = db.query(
        "SELECT r.price, d.item_nbt FROM recycle_shop_items r "
        "JOIN item_definitions d ON r.item_id = d.item_id "
        "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ?",
        shop.dimId, shop.pos.x, shop.pos.y, shop.pos.z
    );

    std::string content = "§e店主: " + ownerName + "\n";
    content += "§7位置: " + dimIdToString(shop.dimId) + " [" + 
               std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", " + 
               std::to_string(shop.pos.z) + "]\n\n";
    
    if (results.empty()) {
        content += "§7该回收商店暂无回收委托。\n";
    } else {
        content += "§a回收物品列表:\n";
        for (const auto& row : results) {
            double price = std::stod(row[0]);
            std::string itemNbtStr = row[1];
            
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (itemPtr) {
                content += "§f- " + std::string(itemPtr->getName()) + " §6[回收价: " + 
                          CT::MoneyFormat::format(price) + "]\n";
            }
        }
    }
    content += "\n§c注意: 需要前往商店位置点击箱子才能回收！";
    fm.setContent(content);

    fm.appendButton("§a传送到商店", [shop](Player& p) {
        p.teleport({(float)shop.pos.x + 0.5f, (float)shop.pos.y + 1.0f, (float)shop.pos.z + 0.5f}, shop.dimId);
        p.sendMessage("§a已传送到回收商店位置，请点击箱子进行回收。");
    });

    fm.appendButton("§7返回列表", [](Player& p) {
        showPublicRecycleShopListForm(p);
    });

    fm.appendButton("§c关闭", [](Player& p) {});
    fm.sendTo(player);
}

} // namespace CT
