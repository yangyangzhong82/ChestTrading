#include "ShopForm.h"
#include "LockForm.h" // 因为需要调用 showChestLockForm
#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/chestprotect.h" // 引入 chestprotect 以使用 ChestType
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/item/Item.h"

namespace CT {

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("商店物品");

    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT slot, item_nbt, price FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (results.empty()) {
        fm.setContent("商店是空的，没有可购买的商品。\n");
    } else {
        for (const auto& row : results) {
            int         slot       = std::stoi(row[0]);
            std::string itemNbtStr = row[1];
            int         price      = std::stoi(row[2]);

            auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
            if (!itemNbt) {
                logger.error("无法解析物品NBT: {}", itemNbtStr);
                continue;
            }
            auto item = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!item) {
                logger.error("无法从NBT创建物品。");
                continue;
            }

            std::string buttonText = std::string(item->getName()) + " x" + std::to_string(item->mCount)
                                   + " §6[价格: " + std::to_string(price) + "]§r";
            std::string itemName = item->getTypeName();
            if (itemName.rfind("minecraft:", 0) == 0) {
                itemName = itemName.substr(10);
            }
            std::string texturePath = ItemTextureManager::getInstance().getTexture(itemName);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [&player, pos, dimId, &region, item = *item, slot, price](Player& p) {
                        showShopItemBuyForm(p, item, pos, dimId, slot, price, region);
                    }
                );
            } else {
                fm.appendButton(buttonText, [&player, pos, dimId, &region, item = *item, slot, price](Player& p) {
                    showShopItemBuyForm(p, item, pos, dimId, slot, price, region);
                });
            }
        }
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子已锁定界面，这里需要获取箱子锁定状态和主人UUID
        // 实际应用中需要从数据库查询这些信息
        std::string ownerUuid = "";              // 非主人，所以UUID为空
        bool        isLocked  = true;            // 假设箱子是锁定的
        ChestType   chestType = ChestType::Shop; // 假设箱子类型是商店
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showShopItemPriceForm(
    Player&          player,
    const ItemStack& item,
    BlockPos         pos,
    int              dimId,
    int              slot,
    BlockSource&     region
) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商品价格");
    fm.appendLabel("你正在为物品: " + std::string(item.getName()) + " x" + std::to_string(item.mCount) + " 设置价格。");
    fm.appendInput("price_input", "请输入价格", "0");

    fm.sendTo(
        player,
        [&player, item, pos, dimId, slot, &region](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了设置价格。");
                return;
            }

            try {
                int price = std::stoi(std::get<std::string>(result.value().at("price_input")));
                if (price < 0) {
                    p.sendMessage("§c价格不能为负数！");
                    return;
                }

                // 获取物品NBT
                auto itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage("§c无法获取物品NBT数据。");
                    return;
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbt);

                // 插入或更新到数据库
                auto&       db  = Sqlite3Wrapper::getInstance();
                std::string sql = "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_nbt, price) VALUES "
                                  "(?, ?, ?, ?, ?, ?, ?) "
                                  "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, slot) DO UPDATE SET item_nbt = "
                                  "excluded.item_nbt, price = excluded.price;";
                if (db.execute(sql, dimId, pos.x, pos.y, pos.z, slot, itemNbtStr, price)) {
                    p.sendMessage("§a物品价格设置成功！价格: " + std::to_string(price));
                } else {
                    p.sendMessage("§c物品价格设置失败！");
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c价格输入无效，请输入一个整数。");
                logger.error("设置物品价格时发生错误: {}", e.what());
            }
        }
    );
}

void showShopItemManageForm(
    Player&          player,
    const ItemStack& item,
    BlockPos         pos,
    int              dimId,
    int              slot,
    BlockSource&     region
) {
    ll::form::SimpleForm fm;
    fm.setTitle("管理商品");

    std::string content = "你正在管理物品: " + std::string(item.getName()) + " x" + std::to_string(item.mCount) + "\n";

    // 查询数据库获取价格
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT price FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND slot = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        slot
    );

    if (!results.empty()) {
        content += "当前价格: §a" + results[0][0] + "§r\n";
    } else {
        content += "当前状态: §7未定价§r\n";
    }
    fm.setContent(content);

    fm.appendButton("设置价格", [&player, item, pos, dimId, slot, &region](Player& p) {
        showShopItemPriceForm(p, item, pos, dimId, slot, region);
    });

    fm.appendButton("移除商品", [&player, pos, dimId, slot, &region](Player& p) {
        auto& db = Sqlite3Wrapper::getInstance();
        if (db.execute(
                "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND slot = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                slot
            )) {
            p.sendMessage("§a商品已成功移除！");
        } else {
            p.sendMessage("§c商品移除失败！");
        }
        showShopChestManageForm(p, pos, dimId, region); // 返回管理界面
    });

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}


void showShopChestManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("管理商店箱子");

    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error("无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return;
    }

    auto chest = static_cast<class ChestBlockActor*>(blockActor);
    if (!chest) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error("无法将 BlockActor 转换为 ChestBlockActor 在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return;
    }

    bool isEmpty = true;
    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& item = chest->getItem(i);
        if (!item.isNull()) {
            isEmpty                = false;
            std::string buttonText = std::string(item.getName()) + " x" + std::to_string(item.mCount);
            std::string itemName   = item.getTypeName();
            if (itemName.rfind("minecraft:", 0) == 0) {
                itemName = itemName.substr(10);
            }
            std::string texturePath = ItemTextureManager::getInstance().getTexture(itemName);

            // 查询数据库获取价格
            auto& db      = Sqlite3Wrapper::getInstance();
            auto  results = db.query(
                "SELECT price FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND slot = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                i
            );

            if (!results.empty()) {
                buttonText += " §a[已定价: " + results[0][0] + "]";
            } else {
                buttonText += " §7[未定价]";
            }

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [&player, pos, dimId, &region, item, i](Player& p) {
                    showShopItemManageForm(p, item, pos, dimId, i, region);
                });
            } else {
                fm.appendButton(buttonText, [&player, pos, dimId, &region, item, i](Player& p) {
                    showShopItemManageForm(p, item, pos, dimId, i, region);
                });
            }
        }
    }

    if (isEmpty) {
        fm.setContent("箱子是空的。\n");
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子管理界面，这里需要根据实际情况调整
        // 暂时返回到 showChestLockForm，需要获取箱子锁定状态和主人UUID
        // 为了简化，这里假设箱子是锁定的，并且主人是当前玩家
        // 实际应用中需要从数据库查询这些信息
        std::string ownerUuid = player.getUuid().asString(); // 假设当前玩家是主人
        bool        isLocked  = true;                        // 假设箱子是锁定的
        ChestType   chestType = ChestType::Shop;             // 假设箱子类型是商店
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showShopItemBuyForm(
    Player&          player,
    const ItemStack& item,
    BlockPos         pos,
    int              dimId,
    int              slot,
    int              price,
    BlockSource&     region
) {
    ll::form::SimpleForm fm;
    fm.setTitle("购买商品");

    std::string content = "你正在购买物品: " + std::string(item.getName()) + " x" + std::to_string(item.mCount) + "\n";
    content += "价格: §6" + std::to_string(price) + "§r\n";
    content += "你的余额: §e" + std::to_string(Economy::getMoney(player)) + "§r\n";
    fm.setContent(content);

    fm.appendButton("购买", [&player, item, pos, dimId, slot, price, &region](Player& p) {
        // 检查玩家金币
        if (!Economy::hasMoney(p, price)) {
            p.sendMessage("§c你的金币不足！");
            return;
        }

        // 检查箱子中是否有足够的物品
        auto* blockActor = region.getBlockEntity(pos);
        if (!blockActor) {
            p.sendMessage("§c无法获取箱子数据。");
            logger.error("购买时无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
            return;
        }
        auto chest = static_cast<class ChestBlockActor*>(blockActor);
        if (!chest) {
            p.sendMessage("§c无法获取箱子数据。");
            logger.error(
                "购买时无法将 BlockActor 转换为 ChestBlockActor 在 ({}, {}, {}) in dim {}",
                pos.x,
                pos.y,
                pos.z,
                dimId
            );
            return;
        }

        const auto& chestItem = chest->getItem(slot);
        if (chestItem.isNull() || chestItem.getTypeName() != item.getTypeName() || chestItem.mCount < item.mCount) {
            p.sendMessage("§c箱子中没有足够的商品！");
            return;
        }

        // 执行购买
        if (Economy::reduceMoney(p, price)) {
            // 给予玩家物品
            ItemStack mutableItem = item; // 创建一个可变副本
            p.add(mutableItem);
            p.refreshInventory();
            // 从箱子中移除物品
            chest->removeItem(slot, item.mCount);
            // 更新数据库 (如果物品数量变为0，则从shop_items中删除)
            if (chestItem.mCount - item.mCount <= 0) {
                auto& db = Sqlite3Wrapper::getInstance();
                db.execute(
                    "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND slot = ?",
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    slot
                );
            }
            p.sendMessage(
                "§a购买成功！你花费了 §6" + std::to_string(price) + "§a 金币购买了 " + std::string(item.getName())
                + " x" + std::to_string(item.mCount) + "。"
            );
        } else {
            p.sendMessage("§c购买失败，金币扣除失败。");
        }
        showShopChestItemsForm(p, pos, dimId, region); // 返回商店浏览界面
    });

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showShopChestItemsForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}

} // namespace CT
