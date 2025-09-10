#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "LockForm.h"

namespace CT {
void showChestLockForm(Player& player, BlockPos pos, int dimId, bool isLocked, const std::string& ownerUuid, BlockSource& region) {
    ll::form::SimpleForm fm;
    std::string player_uuid = player.getUuid().asString();

    if (isLocked) {
        // 箱子已锁定
        if (ownerUuid == player_uuid) {
            // 当前玩家是主人，提供解锁和分享选项
            fm.setTitle("解锁箱子");
            fm.setContent("这个箱子已经被你锁定了，你确定要解锁它吗？");
            fm.appendButton("确定解锁", [pos, dimId, player_uuid, &region](Player& p) {
                logger.info(
                    "玩家 {} 选择解锁位于维度 {} 的 ({}, {}, {}) 的箱子。",
                    player_uuid,
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z
                );
                if (unlockChest(pos, dimId, region)) {
                    logger.info("箱子信息已从数据库中移除。");
                    p.sendMessage("§a箱子已成功解锁！");
                } else {
                    logger.error("箱子信息从数据库中移除失败。");
                    p.sendMessage("§c箱子解锁失败！");
                }
            });
            fm.appendButton("分享箱子", [pos, dimId, ownerUuid, &region](Player& p) {
                logger.info(
                    "玩家 {} 选择分享位于维度 {} 的 ({}, {}, {}) 的箱子。",
                    ownerUuid,
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z
                );
                showShareForm(p, pos, dimId, ownerUuid, region);
            });
        } else {
            // 当前玩家不是主人，不提供解锁和分享选项
            fm.setTitle("箱子已锁定");
            fm.setContent("这个箱子已经被其他玩家锁定了，你无法操作。");
        }
    } else {
        // 箱子未锁定，提供上锁选项
        fm.setTitle("上锁箱子");
        fm.setContent("你确定要上锁这个箱子吗？");
        fm.appendButton("确定上锁", [pos, dimId, player_uuid, &region](Player& p) {
            logger.info(
                "玩家 {} 选择上锁位于维度 {} 的 ({}, {}, {}) 的箱子。",
                player_uuid,
                dimId,
                pos.x,
                pos.y,
                pos.z
            );
            if (lockChest(player_uuid, pos, dimId, region)) {
                logger.info("箱子信息已存入数据库。");
                p.sendMessage("§a箱子已成功上锁！");
            } else {
                logger.error("箱子信息存入数据库失败。");
                p.sendMessage("§c箱子上锁失败！");
            }
        });
    }

    fm.appendButton("取消", [player_uuid](Player& p) {
        logger.info("玩家 {} 取消了操作。", player_uuid);
    });

    fm.sendTo(player);
}
}
