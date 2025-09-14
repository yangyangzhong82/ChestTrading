#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "LockForm.h"

namespace CT {

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

            fm.appendButton("分享箱子", [pos, dimId, ownerUuid, &region](Player& p) {
                showShareForm(p, pos, dimId, ownerUuid, region);
            });

        } else {
            // 当前玩家不是主人
            fm.setTitle("箱子已锁定");
            fm.setContent("这个箱子已经被其他玩家锁定了，你无法操作。");
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
    }

    fm.appendButton("取消", [player_uuid](Player& p) { logger.info("玩家 {} 取消了操作。", player_uuid); });

    fm.sendTo(player);
}
} // namespace CT
