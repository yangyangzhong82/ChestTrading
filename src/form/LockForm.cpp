#include "LockForm.h"
#include "Config/ConfigManager.h"
#include "RecycleForm.h"
#include "ShopForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/economy.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "service/ChestService.h"
#include "service/TextService.h"

namespace CT {

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType);

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
    std::string          player_uuid  = player.getUuid().asString();
    auto&                textService  = TextService::getInstance();
    auto&                chestService = ChestService::getInstance();

    if (isLocked) {
        // 箱子已设置
        if (ownerUuid == player_uuid) {
            // 当前玩家是主人
            std::string typeStr = textService.getChestTypeName(chestType);
            fm.setTitle("箱子管理");
            fm.setContent("这个箱子已被你设置为: " + typeStr + "\n你想做什么？");

            fm.appendButton("移除设置", [pos, dimId](Player& p) {
                auto& region = p.getDimensionBlockSource();
                auto  result = ChestService::getInstance().removeChest(pos, dimId, region);
                p.sendMessage(result.message);
            });

            if (chestType == ChestType::Locked) {
                fm.appendButton("分享箱子", [pos, dimId, ownerUuid](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showShareForm(p, pos, dimId, ownerUuid, region);
                });
            }

            if (chestType == ChestType::Shop) {
                fm.appendButton("管理商店物品", [pos, dimId](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showShopChestManageForm(p, pos, dimId, region);
                });
            } else if (chestType == ChestType::RecycleShop) {
                fm.appendButton("管理回收商店", [pos, dimId](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showRecycleShopManageForm(p, pos, dimId, region);
                });
            }

            fm.appendButton("箱子设置", [pos, dimId, chestType](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showChestSettingsForm(p, pos, dimId, region, chestType);
            });

        } else {
            // 当前玩家不是主人
            fm.setTitle("箱子已锁定");
            fm.setContent("这个箱子已经被其他玩家锁定了，你无法操作。");

            if (chestType == ChestType::Shop) {
                fm.appendButton("浏览商店物品", [pos, dimId](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showShopChestItemsForm(p, pos, dimId, region);
                });
            } else if (chestType == ChestType::RecycleShop) {
                fm.appendButton("浏览回收商店", [pos, dimId](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showRecycleForm(p, pos, dimId, region);
                });
            }
        }
    } else {
        // 箱子未设置，提供设置选项
        fm.setTitle("设置箱子");
        fm.setContent("你希望将这个箱子设置为什么类型？");

        auto createChestHandler =
            [](Player& p, BlockPos pos, int dimId, const std::string& playerUuid, ChestType type, double cost) {
                auto& region = p.getDimensionBlockSource();
                auto& svc    = ChestService::getInstance();
                auto& txt    = TextService::getInstance();

                std::string errorMsg;
                if (!svc.canPlayerCreateChest(playerUuid, type, errorMsg)) {
                    p.sendMessage(errorMsg);
                    return;
                }
                if (!Economy::hasMoney(p, cost)) {
                    p.sendMessage(txt.getMessage(
                        "economy.insufficient",
                        {
                            {"price", MoneyFormat::format(cost)}
                    }
                    ));
                    return;
                }
                if (!Economy::reduceMoney(p, cost)) {
                    p.sendMessage(txt.getMessage("economy.deduct_fail"));
                    return;
                }
                auto result = svc.createChest(playerUuid, pos, dimId, type, region);
                if (result.success) {
                    p.sendMessage(txt.getMessage(
                        "chest.create_success",
                        {
                            {"type",  txt.getChestTypeName(type)},
                            {"price", MoneyFormat::format(cost) }
                    }
                    ));
                } else {
                    Economy::addMoney(p, cost);
                    p.sendMessage(txt.getMessage(
                        "chest.create_fail",
                        {
                            {"type", txt.getChestTypeName(type)}
                    }
                    ));
                }
            };

        fm.appendButton("普通上锁", [pos, dimId, player_uuid, createChestHandler](Player& p) {
            createChestHandler(
                p,
                pos,
                dimId,
                player_uuid,
                ChestType::Locked,
                ConfigManager::getInstance().get().chestCosts.lockedChestCost
            );
        });

        fm.appendButton("设为回收商店", [pos, dimId, player_uuid, createChestHandler](Player& p) {
            createChestHandler(
                p,
                pos,
                dimId,
                player_uuid,
                ChestType::RecycleShop,
                ConfigManager::getInstance().get().chestCosts.recycleShopCost
            );
        });

        fm.appendButton("设为商店", [pos, dimId, player_uuid, createChestHandler](Player& p) {
            createChestHandler(
                p,
                pos,
                dimId,
                player_uuid,
                ChestType::Shop,
                ConfigManager::getInstance().get().chestCosts.shopCost
            );
        });

        fm.appendButton("设为公共箱子", [pos, dimId, player_uuid, createChestHandler](Player& p) {
            createChestHandler(
                p,
                pos,
                dimId,
                player_uuid,
                ChestType::Public,
                ConfigManager::getInstance().get().chestCosts.publicChestCost
            );
        });
    }

    fm.appendButton("取消", [player_uuid](Player& p) { logger.debug("玩家 {} 取消了操作。", player_uuid); });

    fm.sendTo(player);
}

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType) {
    ll::form::CustomForm fm;
    fm.setTitle("箱子设置");

    auto& chestService = ChestService::getInstance();
    auto  config       = chestService.getChestConfig(pos, dimId, region);

    fm.appendToggle("enable_floating_text", "显示悬浮字", config.enableFloatingText);

    bool isShopType = (chestType == ChestType::Shop || chestType == ChestType::RecycleShop);
    if (isShopType) {
        fm.appendToggle("enable_fake_item", "显示假物品", config.enableFakeItem);
        fm.appendToggle("is_public", "公开到商店列表", config.isPublic);
    }

    fm.sendTo(
        player,
        [pos,
         dimId,
         chestType,
         isShopType](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& region = p.getDimensionBlockSource();
            auto& svc    = ChestService::getInstance();
            auto& txt    = TextService::getInstance();

            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                auto info = svc.getChestInfo(pos, dimId, region);
                showChestLockForm(
                    p,
                    pos,
                    dimId,
                    info.has_value(),
                    info ? info->ownerUuid : "",
                    info ? info->type : ChestType::Invalid,
                    region
                );
                return;
            }

            ChestConfigData newConfig;

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

            if (svc.updateChestConfig(pos, dimId, region, newConfig)) {
                p.sendMessage(txt.getMessage("chest.config_saved"));
            } else {
                p.sendMessage(txt.getMessage("chest.config_fail"));
            }

            auto info = svc.getChestInfo(pos, dimId, region);
            showChestLockForm(
                p,
                pos,
                dimId,
                info.has_value(),
                info ? info->ownerUuid : "",
                info ? info->type : ChestType::Invalid,
                region
            );
        }
    );
}

} // namespace CT
