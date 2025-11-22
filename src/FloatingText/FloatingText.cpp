#include "FloatingText/FloatingText.h"
#include "Utils/NetworkPacket.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "debug_shape/api/IDebugShapeDrawer.h"
#include "debug_shape/api/shape/IDebugText.h"
// #include "interaction/chestprotect.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/memory/Hook.h"
#include "logger.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc\network\LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/AddItemActorPacket.h"
#include "mc/world/item/NetworkItemStackDescriptor.h"
#include "mc/world/actor/DataItem.h" 
#include "mc/nbt/CompoundTag.h"    
#include "ll/api/base/Meta.h"       
#include "Utils/NetworkPacket.h"
#include "mc/world/item/ItemStack.h" 
#include "Utils/NbtUtils.h"      
namespace CT {

// 获取单例实例
FloatingTextManager& FloatingTextManager::getInstance() {
    static FloatingTextManager instance;
    return instance;
}

// 添加或更新一个箱子的悬浮字
void FloatingTextManager::addOrUpdateFloatingText(
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    const std::string& text,
    ChestType          type
) {
    auto key = std::make_pair(dimId, pos);
    if (mFloatingTexts.count(key)) {
        // 更新现有悬浮字
        auto& ft = mFloatingTexts.at(key);
        if (ft.text != text) {
            ft.text = text;
            if (ft.debugText) {
                ft.debugText->setText(text);
                ft.debugText->update(); // 更新所有客户端
            }
        }
        ft.type = type; // 更新类型
    } else {
        // 创建新的悬浮字
        auto newText = debug_shape::IDebugText::create(
            Vec3(static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 1.5f, static_cast<float>(pos.z) + 0.5f),
            text
        );
        mFloatingTexts.emplace(key, ChestFloatingText(pos, dimId, ownerUuid, text, type));
        mFloatingTexts.at(key).debugText = std::move(newText);
        debug_shape::IDebugShapeDrawer::getInstance().drawShape(*mFloatingTexts.at(key).debugText); // 绘制给所有客户端
        logger.debug("已为箱子 ({}, {}, {}) in dim {} 创建悬浮字: {}", pos.x, pos.y, pos.z, dimId, text);
    }
}

// 移除一个箱子的悬浮字
void FloatingTextManager::removeFloatingText(BlockPos pos, int dimId) {
    auto key = std::make_pair(dimId, pos);
    if (mFloatingTexts.count(key)) {
        auto& ft = mFloatingTexts.at(key);
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText); // 从所有客户端移除
        }
        mFloatingTexts.erase(key);
        logger.debug("已移除箱子 ({}, {}, {}) in dim {} 的悬浮字。", pos.x, pos.y, pos.z, dimId);
    }
}

// 绘制所有悬浮字给特定玩家
void FloatingTextManager::drawAllFloatingTexts(Player& player) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, player);
        }
    }
}

// 移除所有悬浮字给特定玩家
void FloatingTextManager::removeAllFloatingTexts(Player& player) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, player);
        }
    }
}

// 绘制所有悬浮字给特定维度
void FloatingTextManager::drawAllFloatingTexts(DimensionType dimension) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, dimension);
        }
    }
}

// 移除所有悬浮字给特定维度
void FloatingTextManager::removeAllFloatingTexts(DimensionType dimension) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, dimension);
        }
    }
}

// 绘制所有悬浮字给所有客户端
void FloatingTextManager::drawAllFloatingTexts() {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText);
        }
    }
}

// 移除所有悬浮字给所有客户端
void FloatingTextManager::removeAllFloatingTexts() {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText);
        }
    }
    mFloatingTexts.clear();
}

// 从数据库加载所有已锁定的箱子并创建悬浮字
void FloatingTextManager::loadAllLockedChests() {
    if (mIsLoaded) {
        logger.debug("悬浮字已加载，跳过重复加载。");
        return;
    }

    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query("SELECT player_uuid, dim_id, pos_x, pos_y, pos_z, type FROM chests;");

    for (const auto& row : results) {
        if (row.size() >= 6) {
            std::string ownerUuid = row[0];
            int         dimId     = std::stoi(row[1]);
            int         posX      = std::stoi(row[2]);
            int         posY      = std::stoi(row[3]);
            int         posZ      = std::stoi(row[4]);
            ChestType   chestType = static_cast<ChestType>(std::stoi(row[5]));
            BlockPos    pos(posX, posY, posZ);

            std::string text;
            std::string ownerName = ownerUuid; // 默认使用 UUID
            auto        playerInfo =
                ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
            if (playerInfo) {
                ownerName = playerInfo->name;
            }

            // 根据箱子类型生成不同的悬浮字文本
            switch (chestType) {
            case ChestType::Locked:
                text = "§e[上锁箱子]§r 拥有者: " + ownerName;
                break;
            case ChestType::RecycleShop:
                text = "§a[回收商店]§r 拥有者: " + ownerName;
                break;
            case ChestType::Shop:
                text = "§b[商店箱子]§r 拥有者: " + ownerName;
                break;
            case ChestType::Public:
                text = "§d[公共箱子]";
                break;
            default:
                text = "§f[未知箱子类型]§r 拥有者: " + ownerName;
                break;
            }
            addOrUpdateFloatingText(pos, dimId, ownerUuid, text, chestType);

            // 如果是商店或回收商店，加载物品信息
            if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
                auto key = std::make_pair(dimId, pos); // 重新定义 key
                auto& ft = mFloatingTexts.at(key);
                ft.isDynamic = true;
                std::vector<std::vector<std::string>> itemResults;
                if (chestType == ChestType::Shop) {
                    itemResults = db.query(
                        "SELECT id.item_nbt FROM shop_items si JOIN item_definitions id ON si.item_id = id.item_id WHERE si.dim_id = ? AND si.pos_x = ? AND si.pos_y = ? AND si.pos_z = ?;",
                        dimId, posX, posY, posZ
                    );
                } else { // RecycleShop
                    itemResults = db.query(
                        "SELECT id.item_nbt FROM recycle_shop_items rsi JOIN item_definitions id ON rsi.item_id = id.item_id WHERE rsi.dim_id = ? AND rsi.pos_x = ? AND rsi.pos_y = ? AND rsi.pos_z = ?;",
                        dimId, posX, posY, posZ
                    );
                }

                for (const auto& itemRow : itemResults) {
                    if (!itemRow.empty()) {
                        // 从 NBT 字符串解析物品名称
                        auto nbt = CompoundTag::fromSnbt(itemRow[0]);
                        if (nbt) {
                            nbt->at("Count") = ByteTag(1); // 修复：为创建ItemStack添加Count标签
                            auto itemPtr = NbtUtils::createItemFromNbt(*nbt);
                            if (itemPtr && !itemPtr->isNull()) {
                                std::string itemName = itemPtr->getName();
                                if (itemName.empty()) {
                                    itemName = itemPtr->getTypeName();
                                    logger.warn("为箱子 ({}, {}, {}) in dim {} 加载物品时 item.getName() 返回空，使用 item.getTypeName() 作为备用: {}", pos.x, pos.y, pos.z, dimId, itemName);
                                }
                                ft.itemNames.push_back(itemName);
                                logger.debug("为箱子 ({}, {}, {}) in dim {} 加载物品: {}", pos.x, pos.y, pos.z, dimId, itemName);
                            } else {
                                logger.warn("无法从 NBT 创建有效物品: {}", itemRow[0]);
                            }
                        } else {
                            logger.warn("无法从 NBT 字符串解析物品: {}", itemRow[0]);
                        }
                    }
                }
                if (!ft.itemNames.empty()) {
                    ft.text = (chestType == ChestType::Shop ? "§b[商店箱子]§r 出售: " : "§a[回收商店]§r 回收: ") + ft.itemNames[0];
                    if (ft.debugText) {
                        ft.debugText->setText(ft.text);
                        ft.debugText->update();
                    }
                    logger.debug("箱子 ({}, {}, {}) in dim {} 的动态悬浮字已初始化为: {}", pos.x, pos.y, pos.z, dimId, ft.text);
                } else {
                    ft.text = (chestType == ChestType::Shop ? "§b[商店箱子]§r (无物品)" : "§a[回收商店]§r (无物品)");
                    logger.warn("箱子 ({}, {}, {}) in dim {} 是商店/回收商店，但未加载任何物品名称。", pos.x, pos.y, pos.z, dimId);
                }
            }
        }
    }
    mIsLoaded = true;
    logger.debug("已从数据库加载 {} 个已锁定箱子的悬浮字。", mFloatingTexts.size());

    // 启动动态文本更新循环
    startDynamicTextUpdateLoop();
}

ll::coro::CoroTask<> FloatingTextManager::dynamicTextUpdateCoroutine() {
    while (true) {
        for (auto& pair : mFloatingTexts) {
            auto& ft = pair.second;
            if (ft.isDynamic && !ft.itemNames.empty()) {
                ft.currentItemIndex = (ft.currentItemIndex + 1) % ft.itemNames.size();
                ft.text = (ft.type == ChestType::Shop ? "§b[商店箱子]§r 出售: " : "§a[回收商店]§r 回收: ") + ft.itemNames[ft.currentItemIndex];
                if (ft.debugText) {
                    ft.debugText->setText(ft.text);
                    ft.debugText->update();
                }
            }
        }
        co_await ll::coro::SleepAwaiter(std::chrono::seconds(1)); // 将更新频率改为每1秒
    }
}

void FloatingTextManager::startDynamicTextUpdateLoop() {
    mUpdateTask.emplace(dynamicTextUpdateCoroutine()); // 调用协程函数获取 CoroTask
    mUpdateTask->launch(ll::thread::ServerThreadExecutor::getDefault());
    logger.debug("动态悬浮字更新循环已启动。");
}

// 更新商店/回收商店的悬浮字物品列表
void FloatingTextManager::updateShopFloatingText(BlockPos pos, int dimId, ChestType type) {
    auto key = std::make_pair(dimId, pos);
    if (mFloatingTexts.count(key)) {
        auto& ft = mFloatingTexts.at(key);
        logger.debug("updateShopFloatingText: Updating floating text for chest ({}, {}, {}) in dim {}. Type: {}.", pos.x, pos.y, pos.z, dimId, static_cast<int>(type));

        if (type != ChestType::Shop && type != ChestType::RecycleShop) {
            logger.warn("updateShopFloatingText: 尝试更新非商店/回收商店类型的悬浮字物品列表，操作无效。");
            return;
        }

        ft.itemNames.clear(); // 清空旧的物品名称列表
        Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
        std::vector<std::vector<std::string>> itemResults;

        if (type == ChestType::Shop) {
            itemResults = db.query(
                "SELECT id.item_nbt FROM shop_items si JOIN item_definitions id ON si.item_id = id.item_id WHERE si.dim_id = ? AND si.pos_x = ? AND si.pos_y = ? AND si.pos_z = ?;",
                dimId, pos.x, pos.y, pos.z
            );
        } else { // RecycleShop
            itemResults = db.query(
                "SELECT id.item_nbt FROM recycle_shop_items rsi JOIN item_definitions id ON rsi.item_id = id.item_id WHERE rsi.dim_id = ? AND rsi.pos_x = ? AND rsi.pos_y = ? AND rsi.pos_z = ?;",
                dimId, pos.x, pos.y, pos.z
            );
        }
        logger.debug("updateShopFloatingText: Database query for items returned {} results.", itemResults.size());

        for (const auto& itemRow : itemResults) {
            if (!itemRow.empty()) {
                logger.debug("updateShopFloatingText: Processing item NBT string: {}", itemRow[0]);
                auto nbt = CT::NbtUtils::parseSNBT(itemRow[0]); // 使用 CT::NbtUtils::parseSNBT
                if (nbt) {
                    nbt->at("Count") = ByteTag(1); // 修复：为创建ItemStack添加Count标签
                    auto itemPtr = CT::NbtUtils::createItemFromNbt(*nbt); // 使用 CT::NbtUtils::createItemFromNbt
                    if (itemPtr && !itemPtr->isNull()) {
                        std::string itemName = itemPtr->getName();
                        if (itemName.empty()) {
                            itemName = itemPtr->getTypeName(); // 如果 getName() 为空，使用 getTypeName() 作为备用
                            logger.warn("updateShopFloatingText: item.getName() 返回空，使用 item.getTypeName() 作为备用: {}", itemName);
                        }
                        ft.itemNames.push_back(itemName);
                        logger.debug("updateShopFloatingText: Added item name: {} to list.", itemName);
                    } else {
                        logger.warn("updateShopFloatingText: 无法从 NBT 创建有效物品: {}", itemRow[0]);
                    }
                } else {
                    logger.warn("updateShopFloatingText: 无法从 NBT 字符串解析物品: {}", itemRow[0]);
                }
            }
        }
        logger.debug("updateShopFloatingText: ft.itemNames contains {} items after database query.", ft.itemNames.size());

        // 更新悬浮字文本
        if (!ft.itemNames.empty()) {
            ft.currentItemIndex = 0; // 重置索引
            ft.text = (type == ChestType::Shop ? "§b[商店箱子]§r 出售: " : "§a[回收商店]§r 回收: ") + ft.itemNames[0];
            logger.debug("updateShopFloatingText: 箱子 ({}, {}, {}) in dim {} 的动态悬浮字已更新为: {}", pos.x, pos.y, pos.z, dimId, ft.text);
        } else {
            ft.text = (type == ChestType::Shop ? "§b[商店箱子]§r (无物品)" : "§a[回收商店]§r (无物品)");
            logger.debug("updateShopFloatingText: 箱子 ({}, {}, {}) in dim {} 的动态悬浮字已更新为: {}", pos.x, pos.y, pos.z, dimId, ft.text);
        }

        if (ft.debugText) {
            ft.debugText->setText(ft.text);
            ft.debugText->update(); // 立即更新所有客户端
        }
    } else {
        logger.warn("updateShopFloatingText: 尝试更新不存在的悬浮字 ({}, {}, {}) in dim {}。", pos.x, pos.y, pos.z, dimId);
    }
}


void registerPlayerConnectionListener() {
    ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerJoinEvent>(
        [](ll::event::player::PlayerJoinEvent& event) {
            auto& player = event.self(); // 修正 event.player 为 event.self()
            // 在第一个玩家加入时加载所有悬浮字
            if (!FloatingTextManager::getInstance().mIsLoaded) {
                FloatingTextManager::getInstance().loadAllLockedChests();
            }

            FloatingTextManager::getInstance()
                .drawAllFloatingTexts(player);
            logger.debug("玩家 {} 加入游戏，已为其绘制所有悬浮字。", player.getRealName());
        }
    );
}


}
