#include "FloatingText/FloatingText.h"
#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "Utils/NetworkPacket.h"
#include "Utils/fakeitem.h"
#include "db/Sqlite3Wrapper.h"
#include "debug_shape/api/IDebugShapeDrawer.h"
#include "debug_shape/api/shape/IDebugText.h"
#include "ll/api/base/Meta.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "logger.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/AddItemActorPacket.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/NetworkItemStackDescriptor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/ChangeDimensionRequest.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/network/LoopbackPacketSender.h"



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

// 为特定玩家移除特定维度的所有悬浮字
void FloatingTextManager::removeAllFloatingTexts(Player& player, DimensionType dimension) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, player);
        }
    }
}

// 为特定玩家绘制特定维度的所有悬浮字
void FloatingTextManager::drawAllFloatingTexts(Player& player, DimensionType dimension) {
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, player);
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

    const auto&                           config  = CT::ConfigManager::getInstance().get();
    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query("SELECT player_uuid, dim_id, pos_x, pos_y, pos_z, type, enable_floating_text, enable_fake_item FROM chests;");

    for (const auto& row : results) {
        if (row.size() >= 6) {
            std::string ownerUuid = row[0];
            int         dimId     = std::stoi(row[1]);
            int         posX      = std::stoi(row[2]);
            int         posY      = std::stoi(row[3]);
            int         posZ      = std::stoi(row[4]);
            ChestType   chestType = static_cast<ChestType>(std::stoi(row[5]));
            bool        enableFloatingText = (row.size() >= 7) ? (std::stoi(row[6]) != 0) : true;
            bool        enableFakeItem     = (row.size() >= 8) ? (std::stoi(row[7]) != 0) : true;
            BlockPos    pos(posX, posY, posZ);

            // 如果单箱子配置禁用悬浮字，跳过
            if (!enableFloatingText) continue;

            std::string text;
            std::string ownerName = ownerUuid;
            auto        playerInfo =
                ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
            if (playerInfo) {
                ownerName = playerInfo->name;
            }

            // 根据箱子类型生成不同的悬浮字文本
            switch (chestType) {
            case ChestType::Locked:
                if (!config.floatingText.enableLockedChest) continue;
                text = "§e[上锁箱子]§r 拥有者: " + ownerName;
                break;
            case ChestType::RecycleShop:
                if (!config.floatingText.enableRecycleShop) continue;
                text = "§a[回收商店]§r 拥有者: " + ownerName;
                break;
            case ChestType::Shop:
                if (!config.floatingText.enableShopChest) continue;
                text = "§b[商店箱子]§r 拥有者: " + ownerName;
                break;
            case ChestType::Public:
                if (!config.floatingText.enablePublicChest) continue;
                text = "§d[公共箱子]";
                break;
            default:
                text = "§f[未知箱子类型]§r 拥有者: " + ownerName;
                break;
            }
            addOrUpdateFloatingText(pos, dimId, ownerUuid, text, chestType);

            // 如果是商店或回收商店，加载物品信息
            if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
                auto key = std::make_pair(dimId, pos);
                auto& ft = mFloatingTexts.at(key);
                ft.isDynamic = true;
                ft.enableFakeItem = enableFakeItem; // 设置单箱子假物品配置
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
                                ft.items.push_back(*itemPtr); // 存储ItemStack用于假物品显示
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
    logger.debug("dynamicTextUpdateCoroutine: 协程开始运行");
    while (true) {
        logger.trace("dynamicTextUpdateCoroutine: 开始更新循环，共 {} 个悬浮字", mFloatingTexts.size());
        for (auto& pair : mFloatingTexts) {
            auto& ft = pair.second;
            if (ft.isDynamic && !ft.itemNames.empty()) {
                ft.currentItemIndex = (ft.currentItemIndex + 1) % ft.itemNames.size();
                ft.text = (ft.type == ChestType::Shop ? "§b[商店箱子]§r 出售: " : "§a[回收商店]§r 回收: ") + ft.itemNames[ft.currentItemIndex];
                logger.trace("dynamicTextUpdateCoroutine: 更新悬浮字 ({},{},{}) 到物品索引 {}: {}", ft.pos.x, ft.pos.y, ft.pos.z, ft.currentItemIndex, ft.text);
                if (ft.debugText) {
                    ft.debugText->setText(ft.text);
                    ft.debugText->update();
                }
            }
        }
        // 更新所有玩家的假物品
        updateFakeItemsForAllPlayers();
        co_await ll::coro::SleepAwaiter(std::chrono::seconds(CT::ConfigManager::getInstance().get().floatingTextUpdateIntervalSeconds));
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
        ft.items.clear();     // 清空旧的物品列表
        ft.isDynamic = true;  // 确保标记为动态悬浮字
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
                auto nbt = CT::NbtUtils::parseSNBT(itemRow[0]); 
                if (nbt) {
                    nbt->at("Count") = ByteTag(1); 
                    auto itemPtr = CT::NbtUtils::createItemFromNbt(*nbt); 
                    if (itemPtr && !itemPtr->isNull()) {
                        std::string itemName = itemPtr->getName();
                        if (itemName.empty()) {
                            itemName = itemPtr->getTypeName(); 
                            logger.warn("updateShopFloatingText: item.getName() 返回空，使用 item.getTypeName() 作为备用: {}", itemName);
                        }
                        ft.itemNames.push_back(itemName);
                        ft.items.push_back(*itemPtr); // 存储ItemStack用于假物品显示
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
        
        // 立即更新所有玩家的假物品
        updateFakeItemsForAllPlayers();
    } else {
        logger.warn("updateShopFloatingText: 尝试更新不存在的悬浮字 ({}, {}, {}) in dim {}。", pos.x, pos.y, pos.z, dimId);
    }
}


// 发送假物品给玩家
void FloatingTextManager::sendFakeItemToPlayer(Player& player, ChestFloatingText& ft) {
    // 检查全局配置和单箱子配置
    if (ft.items.empty() || !ft.isDynamic || !CT::ConfigManager::getInstance().get().floatingText.enableFakeItem || !ft.enableFakeItem)
        return;

    // 边界检查
    if (ft.currentItemIndex >= ft.items.size()) {
        ft.currentItemIndex = 0;
    }
    
    std::string playerUuid = player.getUuid().asString();
    
    // 先移除旧的假物品
    removeFakeItemFromPlayer(player, ft);
    
    // 发送新的假物品
    Vec3 itemPos(
        static_cast<float>(ft.pos.x) + 0.5f,
        static_cast<float>(ft.pos.y) + 1.0f,
        static_cast<float>(ft.pos.z) + 0.5f
    );
    
    auto& currentItem = ft.items[ft.currentItemIndex];
    auto id = AddFakeitem(itemPos, player, player.getDimensionBlockSource(), currentItem);
    ft.playerFakeItemIds[playerUuid] = id;
}

// 移除玩家的假物品
void FloatingTextManager::removeFakeItemFromPlayer(Player& player, ChestFloatingText& ft) {
    std::string playerUuid = player.getUuid().asString();
    auto it = ft.playerFakeItemIds.find(playerUuid);
    if (it != ft.playerFakeItemIds.end()) {
        RemoveFakeitem(player, it->second);
        ft.playerFakeItemIds.erase(it);
    }
}

// 更新所有玩家的假物品
void FloatingTextManager::updateFakeItemsForAllPlayers() {
    if (!CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) return;
    auto level = ll::service::getLevel();
    if (!level) return;

    level->forEachPlayer([this](Player& player) {
        int playerDimId = player.getDimensionId().id;
        for (auto& pair : mFloatingTexts) {
            auto& ft = pair.second;
            if (ft.isDynamic && !ft.items.empty() && ft.dimId == playerDimId) {
                sendFakeItemToPlayer(player, ft);
            }
        }
        return true;
    });
}

void registerPlayerConnectionListener() {
    ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerJoinEvent>(
        [](ll::event::player::PlayerJoinEvent& event) {
            auto& player = event.self();
            // 在第一个玩家加入时加载所有悬浮字
            if (!FloatingTextManager::getInstance().mIsLoaded) {
                FloatingTextManager::getInstance().loadAllLockedChests();
            }

            FloatingTextManager::getInstance().drawAllFloatingTexts(player);

            // 为玩家发送假物品
            if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
                int playerDimId = player.getDimensionId().id;
                for (auto& [key, ft] : FloatingTextManager::getInstance().mFloatingTexts) {
                    if (ft.isDynamic && !ft.items.empty() && ft.dimId == playerDimId) {
                        FloatingTextManager::getInstance().sendFakeItemToPlayer(player, ft);
                    }
                }
            }

            logger.debug("玩家 {} 加入游戏，已为其绘制所有悬浮字和假物品。", player.getRealName());
        }
    );
}


//玩家跨维度
LL_AUTO_TYPE_INSTANCE_HOOK(
    PlayerChangeDimensionHook5,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$requestPlayerChangeDimension,
    void,
    Player&                  player,
    ChangeDimensionRequest&& changeRequest
) {
    auto fromDim = changeRequest.mFromDimensionId.get();
    auto toDim   = changeRequest.mToDimensionId.get();

    // 移除旧维度的假物品
    if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
        for (auto& pair : FloatingTextManager::getInstance().mFloatingTexts) {
            auto& ft = pair.second;
            if (ft.isDynamic && ft.dimId == static_cast<int>(fromDim)) {
                FloatingTextManager::getInstance().removeFakeItemFromPlayer(player, ft);
            }
        }
    }

    // 在玩家切换维度之前调用原始函数
    origin(player, std::move(changeRequest));

    // 切换维度后，更新悬浮字和假物品
    ll::thread::ServerThreadExecutor::getDefault().execute([&player, fromDim, toDim]() {
        FloatingTextManager::getInstance().removeAllFloatingTexts(player, fromDim);
        FloatingTextManager::getInstance().drawAllFloatingTexts(player, toDim);

        // 发送新维度的假物品
        if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
            for (auto& pair : FloatingTextManager::getInstance().mFloatingTexts) {
                auto& ft = pair.second;
                if (ft.isDynamic && !ft.items.empty() && ft.dimId == static_cast<int>(toDim)) {
                    FloatingTextManager::getInstance().sendFakeItemToPlayer(player, ft);
                }
            }
        }
    });
}
}
