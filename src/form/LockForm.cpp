#include "LockForm.h"
#include "RecycleForm.h"
#include "Utils/ItemTextureManager.h" 
#include "Utils/NbtUtils.h"           
#include "Utils/economy.h"           
#include "db/Sqlite3Wrapper.h"        
#include "interaction/chestprotect.h"
#include "ll/api/form/CustomForm.h" 
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "ShopForm.h"
#include "mc/world/level/block/actor/ChestBlockActor.h" 
#include "mc/world/item/Item.h"
#include "Config/ConfigManager.h"
#include "Utils/MoneyFormat.h"

namespace CT {

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType);

// using namespace CauldronZero::NbtUtils; // 引入 NbtUtils 命名空间

void showChestLockForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    bool               isLocked,
    const std::string& ownerUuid,
    ChestType          chestType,
    BlockSource&       region
) {
    ll::form::SimpleForm fm;
    std::string          player_uuid = player.getUuid().asString();

    if (isLocked) {
        // 箱子已设置
        if (ownerUuid == player_uuid) {
            // 当前玩家是主人
            std::string typeStr;
            switch (chestType) {
            case ChestType::Locked:
                typeStr = "普通锁";
                break;
            case ChestType::RecycleShop:
                typeStr = "回收商店";
                break;
            case ChestType::Shop:
                typeStr = "商店";
                break;
            case ChestType::Public:
                typeStr = "公共箱子";
                break;
            }
            fm.setTitle("箱子管理");
            fm.setContent("这个箱子已被你设置为: " + typeStr + "\n你想做什么？");

            fm.appendButton("移除设置", [pos, dimId, player_uuid, &region](Player& p) {
                if (removeChest(pos, dimId, region)) {
                    p.sendMessage("§a箱子设置已成功移除！");
                } else {
                    p.sendMessage("§c箱子设置移除失败！");
                }
            });

            if (chestType == ChestType::Locked) {
                fm.appendButton("分享箱子", [pos, dimId, ownerUuid, &region](Player& p) {
                    showShareForm(p, pos, dimId, ownerUuid, region);
                });
            }

            if (chestType == ChestType::Shop) {
                fm.appendButton("管理商店物品", [&player, pos, dimId, &region](Player& p) {
                    showShopChestManageForm(p, pos, dimId, region);
                });
            } else if (chestType == ChestType::RecycleShop) {
                fm.appendButton("管理回收商店", [&player, pos, dimId, &region](Player& p) {
                    showRecycleShopManageForm(p, pos, dimId, region);
                });
            }

            fm.appendButton("箱子设置", [pos, dimId, &region, chestType](Player& p) {
                showChestSettingsForm(p, pos, dimId, region, chestType);
            });

        } else {
            // 当前玩家不是主人
            fm.setTitle("箱子已锁定");
            fm.setContent("这个箱子已经被其他玩家锁定了，你无法操作。");

            if (chestType == ChestType::Shop) {
                fm.appendButton("浏览商店物品", [&player, pos, dimId, &region](Player& p) {
                    showShopChestItemsForm(p, pos, dimId, region);
                });
            } else if (chestType == ChestType::RecycleShop) {
                fm.appendButton("浏览回收商店", [&player, pos, dimId, &region](Player& p) {
                    showRecycleForm(p, pos, dimId, region);
                });
            }
        }
    } else {
        // 箱子未设置，提供设置选项
        fm.setTitle("设置箱子");
        fm.setContent("你希望将这个箱子设置为什么类型？");

    fm.appendButton("普通上锁", [pos, dimId, player_uuid, &region](Player& p) {
        std::string errorMsg;
        if (!canPlayerCreateChest(player_uuid, ChestType::Locked, errorMsg)) {
            p.sendMessage(errorMsg);
            return;
        }
        double cost = ConfigManager::getInstance().get().chestCosts.lockedChestCost;
        if (!Economy::hasMoney(p, cost)) {
            p.sendMessage("§c金钱不足！需要 " + MoneyFormat::format(cost));
            return;
        }
        if (!Economy::reduceMoney(p, cost)) {
            p.sendMessage("§c扣除金钱失败！");
            return;
        }
        if (setChest(player_uuid, pos, dimId, region, ChestType::Locked)) {
            p.sendMessage("§a箱子已成功上锁！花费 " + MoneyFormat::format(cost));
        } else {
            Economy::addMoney(p, cost);
            p.sendMessage("§c箱子上锁失败！已退还金钱");
        }
    });

    fm.appendButton("设为回收商店", [pos, dimId, player_uuid, &region](Player& p) {
        std::string errorMsg;
        if (!canPlayerCreateChest(player_uuid, ChestType::RecycleShop, errorMsg)) {
            p.sendMessage(errorMsg);
            return;
        }
        double cost = ConfigManager::getInstance().get().chestCosts.recycleShopCost;
        if (!Economy::hasMoney(p, cost)) {
            p.sendMessage("§c金钱不足！需要 " + MoneyFormat::format(cost));
            return;
        }
        if (!Economy::reduceMoney(p, cost)) {
            p.sendMessage("§c扣除金钱失败！");
            return;
        }
        if (setChest(player_uuid, pos, dimId, region, ChestType::RecycleShop)) {
            p.sendMessage("§a箱子已成功设为回收商店！花费 " + MoneyFormat::format(cost));
        } else {
            Economy::addMoney(p, cost);
            p.sendMessage("§c设置回收商店失败！已退还金钱");
        }
    });

    fm.appendButton("设为商店", [pos, dimId, player_uuid, &region](Player& p) {
        std::string errorMsg;
        if (!canPlayerCreateChest(player_uuid, ChestType::Shop, errorMsg)) {
            p.sendMessage(errorMsg);
            return;
        }
        double cost = ConfigManager::getInstance().get().chestCosts.shopCost;
        if (!Economy::hasMoney(p, cost)) {
            p.sendMessage("§c金钱不足！需要 " + MoneyFormat::format(cost));
            return;
        }
        if (!Economy::reduceMoney(p, cost)) {
            p.sendMessage("§c扣除金钱失败！");
            return;
        }
        if (setChest(player_uuid, pos, dimId, region, ChestType::Shop)) {
            p.sendMessage("§a箱子已成功设为商店！花费 " + MoneyFormat::format(cost));
        } else {
            Economy::addMoney(p, cost);
            p.sendMessage("§c设置商店失败！已退还金钱");
        }
    });

    fm.appendButton("设为公共箱子", [pos, dimId, player_uuid, &region](Player& p) {
        std::string errorMsg;
        if (!canPlayerCreateChest(player_uuid, ChestType::Public, errorMsg)) {
            p.sendMessage(errorMsg);
            return;
        }
        double cost = ConfigManager::getInstance().get().chestCosts.publicChestCost;
        if (!Economy::hasMoney(p, cost)) {
            p.sendMessage("§c金钱不足！需要 " + MoneyFormat::format(cost));
            return;
        }
        if (!Economy::reduceMoney(p, cost)) {
            p.sendMessage("§c扣除金钱失败！");
            return;
        }
        if (setChest(player_uuid, pos, dimId, region, ChestType::Public)) {
            p.sendMessage("§a箱子已成功设为公共箱子！花费 " + MoneyFormat::format(cost));
        } else {
            Economy::addMoney(p, cost);
            p.sendMessage("§c设置公共箱子失败！已退还金钱");
        }
    });
    }

    fm.appendButton("取消", [player_uuid](Player& p) { logger.debug("玩家 {} 取消了操作。", player_uuid); });

    fm.sendTo(player);
}

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType) {
    ll::form::CustomForm fm;
    fm.setTitle("箱子设置");

    ChestConfig config = getChestConfig(pos, dimId, region);

    fm.appendToggle("enable_floating_text", "显示悬浮字", config.enableFloatingText);

    bool isShopType = (chestType == ChestType::Shop || chestType == ChestType::RecycleShop);
    if (isShopType) {
        fm.appendToggle("enable_fake_item", "显示假物品", config.enableFakeItem);
        fm.appendToggle("is_public", "公开到商店列表", config.isPublic);
    }

    fm.sendTo(player, [pos, dimId, &region, chestType, isShopType](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        if (!result.has_value()) {
            p.sendMessage("§c你取消了设置。");
            auto [isLocked, ownerUuid, type] = getChestDetails(pos, dimId, region);
            showChestLockForm(p, pos, dimId, isLocked, ownerUuid, type, region);
            return;
        }

        ChestConfig newConfig;
        
        auto ftIt = result->find("enable_floating_text");
        if (ftIt != result->end() && std::holds_alternative<uint64>(ftIt->second)) {
            newConfig.enableFloatingText = (std::get<uint64>(ftIt->second) != 0);
        }

        if (isShopType) {
            auto fiIt = result->find("enable_fake_item");
            if (fiIt != result->end() && std::holds_alternative<uint64>(fiIt->second)) {
                newConfig.enableFakeItem = (std::get<uint64>(fiIt->second) != 0);
            }
            auto ipIt = result->find("is_public");
            if (ipIt != result->end() && std::holds_alternative<uint64>(ipIt->second)) {
                newConfig.isPublic = (std::get<uint64>(ipIt->second) != 0);
            }
        }

        if (setChestConfig(pos, dimId, region, newConfig)) {
            p.sendMessage("§a箱子设置已保存！");
        } else {
            p.sendMessage("§c箱子设置保存失败！");
        }

        auto [isLocked, ownerUuid, type] = getChestDetails(pos, dimId, region);
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, type, region);
    });
}

} // namespace CT
