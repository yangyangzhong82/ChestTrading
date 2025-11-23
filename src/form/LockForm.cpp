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
#include "ShopForm.h" // 引入 ShopForm
#include "mc/world/level/block/actor/ChestBlockActor.h" 
#include "mc/world/item/Item.h"


namespace CT {

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
                    showRecycleShopManageForm(p, pos, dimId, region); // 调用新的管理表单
                });
            }

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
            if (setChest(player_uuid, pos, dimId, region, ChestType::Locked)) {
                p.sendMessage("§a箱子已成功上锁！");
            } else {
                p.sendMessage("§c箱子上锁失败！");
            }
        });

        fm.appendButton("设为回收商店", [pos, dimId, player_uuid, &region](Player& p) {
            if (setChest(player_uuid, pos, dimId, region, ChestType::RecycleShop)) {
                p.sendMessage("§a箱子已成功设为回收商店！");
            } else {
                p.sendMessage("§c设置回收商店失败！");
            }
        });

        fm.appendButton("设为商店", [pos, dimId, player_uuid, &region](Player& p) {
            if (setChest(player_uuid, pos, dimId, region, ChestType::Shop)) {
                p.sendMessage("§a箱子已成功设为商店！");
            } else {
                p.sendMessage("§c设置商店失败！");
            }
        });

        fm.appendButton("设为公共箱子", [pos, dimId, player_uuid, &region](Player& p) {
            if (setChest(player_uuid, pos, dimId, region, ChestType::Public)) {
                p.sendMessage("§a箱子已成功设为公共箱子！");
            } else {
                p.sendMessage("§c设置公共箱子失败！");
            }
        });
    }

    fm.appendButton("取消", [player_uuid](Player& p) { logger.info("玩家 {} 取消了操作。", player_uuid); });

    fm.sendTo(player);
}


} // namespace CT
