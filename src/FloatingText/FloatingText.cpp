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
#include "mc/network/LoopbackPacketSender.h"
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
#include "service/TextService.h"



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
    std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 写锁
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
        // 创建新的悬浮字（使用常量定义的偏移量）
        auto newText = debug_shape::IDebugText::create(
            Vec3(
                static_cast<float>(pos.x) + FloatingTextConstants::HORIZONTAL_OFFSET,
                static_cast<float>(pos.y) + FloatingTextConstants::TEXT_HEIGHT_OFFSET,
                static_cast<float>(pos.z) + FloatingTextConstants::HORIZONTAL_OFFSET
            ),
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
    std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 写锁
    auto key = std::make_pair(dimId, pos);
    if (mFloatingTexts.count(key)) {
        auto& ft = mFloatingTexts.at(key);
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText); // 从所有客户端移除
        }
        // 移除所有玩家的假物品
        if (!ft.playerFakeItemIds.empty()) {
            auto level = ll::service::getLevel();
            if (level) {
                for (auto& [playerUuid, fakeItemId] : ft.playerFakeItemIds) {
                    auto* playerPtr = level->getPlayer(playerUuid);
                    if (playerPtr) {
                        RemoveFakeitem(*playerPtr, fakeItemId);
                    }
                }
            }
        }
        mFloatingTexts.erase(key);
        logger.debug("已移除箱子 ({}, {}, {}) in dim {} 的悬浮字。", pos.x, pos.y, pos.z, dimId);
    }
}

// 绘制所有悬浮字给特定玩家
void FloatingTextManager::drawAllFloatingTexts(Player& player) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, player);
        }
    }
}

// 移除所有悬浮字给特定玩家
void FloatingTextManager::removeAllFloatingTexts(Player& player) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, player);
        }
    }
}

// 为特定玩家移除特定维度的所有悬浮字
void FloatingTextManager::removeAllFloatingTexts(Player& player, DimensionType dimension) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, player);
        }
    }
}

// 为特定玩家绘制特定维度的所有悬浮字
void FloatingTextManager::drawAllFloatingTexts(Player& player, DimensionType dimension) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, player);
        }
    }
}

// 绘制所有悬浮字给特定维度
void FloatingTextManager::drawAllFloatingTexts(DimensionType dimension) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText, dimension);
        }
    }
}

// 移除所有悬浮字给特定维度
void FloatingTextManager::removeAllFloatingTexts(DimensionType dimension) {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.dimId == static_cast<int>(dimension) && ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText, dimension);
        }
    }
}

// 绘制所有悬浮字给所有客户端
void FloatingTextManager::drawAllFloatingTexts() {
    std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().drawShape(*ft.debugText);
        }
    }
}

// 移除所有悬浮字给所有客户端
void FloatingTextManager::removeAllFloatingTexts() {
    std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 写锁
    for (auto const& [key, ft] : mFloatingTexts) {
        if (ft.debugText) {
            debug_shape::IDebugShapeDrawer::getInstance().removeShape(*ft.debugText);
        }
    }
    mFloatingTexts.clear();
}

// 从数据库加载所有箱子并创建悬浮字
void FloatingTextManager::loadAllChests() {
    if (mIsLoaded) {
        logger.debug("悬浮字已加载，跳过重复加载。");
        return;
    }

    const auto&                           config  = CT::ConfigManager::getInstance().get();
    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, dim_id, pos_x, pos_y, pos_z, type, enable_floating_text, enable_fake_item FROM chests;"
    );

    // 优化：批量加载所有商店和回收商店的物品，避免 N+1 查询问题
    // 使用 UNION ALL 一次性获取所有物品数据，然后在内存中按位置分组
    std::vector<std::vector<std::string>> allItemsResults = db.query(
        "SELECT si.dim_id, si.pos_x, si.pos_y, si.pos_z, id.item_nbt, 'shop' as source "
        "FROM shop_items si "
        "JOIN item_definitions id ON si.item_id = id.item_id "
        "UNION ALL "
        "SELECT rsi.dim_id, rsi.pos_x, rsi.pos_y, rsi.pos_z, id.item_nbt, 'recycle' as source "
        "FROM recycle_shop_items rsi "
        "JOIN item_definitions id ON rsi.item_id = id.item_id "
        "ORDER BY dim_id, pos_x, pos_y, pos_z;"
    );

    // 按位置分组物品数据，key = "dimId:x:y:z", value = vector<item_nbt>
    std::unordered_map<std::string, std::vector<std::string>> itemsByPosition;
    for (const auto& itemRow : allItemsResults) {
        if (itemRow.size() >= FloatingTextConstants::MIN_ITEM_FIELDS) {
            std::string posKey =
                itemRow[0] + ":" + itemRow[1] + ":" + itemRow[2] + ":" + itemRow[3]; // dimId:x:y:z
            itemsByPosition[posKey].push_back(itemRow[4]);                           // item_nbt
        }
    }
    logger.debug("批量加载完成，共 {} 个位置有物品数据。", itemsByPosition.size());

    std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 写锁保护整个加载过程

    for (const auto& row : results) {
        if (row.size() >= FloatingTextConstants::MIN_CHEST_FIELDS) {
            std::string ownerUuid          = row[0];
            int         dimId              = std::stoi(row[1]);
            int         posX               = std::stoi(row[2]);
            int         posY               = std::stoi(row[3]);
            int         posZ               = std::stoi(row[4]);
            ChestType   chestType          = static_cast<ChestType>(std::stoi(row[5]));
            bool        enableFloatingText = (row.size() >= 7) ? (std::stoi(row[6]) != 0) : true;
            bool        enableFakeItem     = (row.size() >= 8) ? (std::stoi(row[7]) != 0) : true;
            BlockPos    pos(posX, posY, posZ);

            // 如果单箱子配置禁用悬浮字，跳过
            if (!enableFloatingText) continue;

            std::string text;
            std::string ownerName  = ownerUuid;
            auto        playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
            if (playerInfo) {
                ownerName = playerInfo->name;
            }

            // 根据箱子类型检查配置
            switch (chestType) {
            case ChestType::Locked:
                if (!config.floatingText.enableLockedChest) continue;
                break;
            case ChestType::RecycleShop:
                if (!config.floatingText.enableRecycleShop) continue;
                break;
            case ChestType::Shop:
                if (!config.floatingText.enableShopChest) continue;
                break;
            case ChestType::Public:
                if (!config.floatingText.enablePublicChest) continue;
                break;
            default:
                break;
            }
            text = TextService::getInstance().generateChestText(chestType, ownerName);

            // 内联 addOrUpdateFloatingText 的逻辑以避免死锁
            auto key = std::make_pair(dimId, pos);
            if (mFloatingTexts.count(key)) {
                // 更新现有悬浮字
                auto& ft = mFloatingTexts.at(key);
                if (ft.text != text) {
                    ft.text = text;
                    if (ft.debugText) {
                        ft.debugText->setText(text);
                        ft.debugText->update();
                    }
                }
                ft.type = chestType;
            } else {
                // 创建新的悬浮字（使用常量定义的偏移量）
                auto newText = debug_shape::IDebugText::create(
                    Vec3(
                        static_cast<float>(pos.x) + FloatingTextConstants::HORIZONTAL_OFFSET,
                        static_cast<float>(pos.y) + FloatingTextConstants::TEXT_HEIGHT_OFFSET,
                        static_cast<float>(pos.z) + FloatingTextConstants::HORIZONTAL_OFFSET
                    ),
                    text
                );
                mFloatingTexts.emplace(key, ChestFloatingText(pos, dimId, ownerUuid, text, chestType));
                mFloatingTexts.at(key).debugText = std::move(newText);
                debug_shape::IDebugShapeDrawer::getInstance().drawShape(*mFloatingTexts.at(key).debugText);
                logger.debug("已为箱子 ({}, {}, {}) in dim {} 创建悬浮字: {}", pos.x, pos.y, pos.z, dimId, text);
            }

            // 如果是商店或回收商店，从预加载的数据中获取物品信息
            if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
                // 复用上面的 key 变量，避免重复声明
                auto& ft          = mFloatingTexts.at(key);
                ft.isDynamic      = true;
                ft.enableFakeItem = enableFakeItem; // 设置单箱子假物品配置

                // 构建位置键
                std::string posKey =
                    std::to_string(dimId) + ":" + std::to_string(posX) + ":" + std::to_string(posY) + ":"
                    + std::to_string(posZ);

                // 从预加载的数据中获取物品
                auto itemsIt = itemsByPosition.find(posKey);
                if (itemsIt != itemsByPosition.end()) {
                    for (const auto& itemNbt : itemsIt->second) {
                        // 从 NBT 字符串解析物品名称
                        auto nbt = CompoundTag::fromSnbt(itemNbt);
                        if (nbt) {
                            nbt->at("Count") = ByteTag(1); // 修复：为创建ItemStack添加Count标签
                            auto itemPtr     = NbtUtils::createItemFromNbt(*nbt);
                            if (itemPtr && !itemPtr->isNull()) {
                                std::string itemName = itemPtr->getName();
                                if (itemName.empty()) {
                                    itemName = itemPtr->getTypeName();
                                    logger.warn(
                                        "为箱子 ({}, {}, {}) in dim {} 加载物品时 item.getName() 返回空，使用 "
                                        "item.getTypeName() 作为备用: {}",
                                        pos.x,
                                        pos.y,
                                        pos.z,
                                        dimId,
                                        itemName
                                    );
                                }
                                ft.itemNames.push_back(itemName);
                                ft.items.push_back(*itemPtr); // 存储ItemStack用于假物品显示
                                logger.debug(
                                    "为箱子 ({}, {}, {}) in dim {} 加载物品: {}",
                                    pos.x,
                                    pos.y,
                                    pos.z,
                                    dimId,
                                    itemName
                                );
                            } else {
                                logger.warn("无法从 NBT 创建有效物品: {}", itemNbt);
                            }
                        } else {
                            logger.warn("无法从 NBT 字符串解析物品: {}", itemNbt);
                        }
                    }
                }

                if (!ft.itemNames.empty()) {
                    ft.text = TextService::getInstance().generateDynamicShopText(chestType, ft.itemNames[0]);
                    if (ft.debugText) {
                        ft.debugText->setText(ft.text);
                        ft.debugText->update();
                    }
                    logger.debug(
                        "箱子 ({}, {}, {}) in dim {} 的动态悬浮字已初始化为: {}",
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId,
                        ft.text
                    );
                } else {
                    ft.text = TextService::getInstance().generateEmptyShopText(chestType);
                    logger.debug(
                        "箱子 ({}, {}, {}) in dim {} 是商店/回收商店，但未加载任何物品名称。",
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId
                    );
                }
            }
        }
    }
    mIsLoaded = true;
    logger.debug("已从数据库加载 {} 个箱子的悬浮字。", mFloatingTexts.size());

    // 启动动态文本更新循环
    startDynamicTextUpdateLoop();
}

ll::coro::CoroTask<> FloatingTextManager::dynamicTextUpdateCoroutine() {
    logger.debug("dynamicTextUpdateCoroutine: 协程开始运行");
    bool updateText = true; // 交替标志：true更新悬浮字，false更新假物品
    while (!mShouldStopUpdate.load()) {
        // 优化：使用结构体批量复制需要更新的数据，减少锁持有时间
        struct UpdateInfo {
            std::pair<int, BlockPos> key;
            size_t                   currentItemIndex;
            size_t                   currentFakeItemIndex;
            size_t                   itemNamesSize;
            size_t                   itemsSize;
            ChestType                type;
        };
        std::vector<UpdateInfo> toUpdate;

        // 第一阶段：快速收集需要更新的数据（读锁）
        {
            std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex);
            logger.trace(
                "dynamicTextUpdateCoroutine: 开始更新循环，共 {} 个悬浮字, updateText={}",
                mFloatingTexts.size(),
                updateText
            );

            toUpdate.reserve(mFloatingTexts.size()); // 预分配内存避免重新分配
            for (const auto& [key, ft] : mFloatingTexts) {
                if (ft.isDynamic && !ft.itemNames.empty()) {
                    toUpdate.push_back({
                        key,
                        ft.currentItemIndex,
                        ft.currentFakeItemIndex,
                        ft.itemNames.size(),
                        ft.items.size(),
                        ft.type
                    });
                }
            }
        } // 快速释放读锁

        // 第二阶段：无锁计算新的索引（不需要持锁）
        for (auto& info : toUpdate) {
            if (updateText && info.itemNamesSize > 0) {
                // 计算下一个物品索引
                info.currentItemIndex = (info.currentItemIndex + 1) % info.itemNamesSize;
            } else if (!updateText && info.itemsSize > 0) {
                // 计算下一个假物品索引
                info.currentFakeItemIndex = (info.currentFakeItemIndex + 1) % info.itemsSize;
            }
        }

        // 第三阶段：批量写回更新（写锁，快速操作）
        {
            std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex);
            for (const auto& info : toUpdate) {
                // 检查键是否仍然存在（可能在释放锁期间被删除）
                auto it = mFloatingTexts.find(info.key);
                if (it == mFloatingTexts.end()) continue;

                auto& ft = it->second;
                // 再次检查是否仍为动态悬浮字
                if (!ft.isDynamic) continue;

                if (updateText && !ft.itemNames.empty()) {
                    // 边界检查（防止在锁释放期间itemNames被修改）
                    if (info.currentItemIndex >= ft.itemNames.size()) continue;

                    ft.currentItemIndex = info.currentItemIndex;
                    ft.text = TextService::getInstance().generateDynamicShopText(
                        ft.type,
                        ft.itemNames[ft.currentItemIndex]
                    );
                    logger.trace(
                        "dynamicTextUpdateCoroutine: 更新悬浮字 ({},{},{}) 到物品索引 {}: {}",
                        ft.pos.x,
                        ft.pos.y,
                        ft.pos.z,
                        ft.currentItemIndex,
                        ft.text
                    );
                    if (ft.debugText) {
                        ft.debugText->setText(ft.text);
                        ft.debugText->update();
                    }
                } else if (!updateText && !ft.items.empty()) {
                    // 边界检查
                    if (info.currentFakeItemIndex >= ft.items.size()) continue;

                    ft.currentFakeItemIndex = info.currentFakeItemIndex;
                }
            }
        } // 快速释放写锁

        if (!updateText) {
            // 更新所有玩家的假物品
            updateFakeItemsForAllPlayers();
        }
        updateText = !updateText; // 交替
        co_await ll::coro::SleepAwaiter(
            std::chrono::seconds(CT::ConfigManager::getInstance().get().floatingTextUpdateIntervalSeconds)
        );
    }
}

void FloatingTextManager::startDynamicTextUpdateLoop() {
    mShouldStopUpdate.store(false);
    mUpdateTask.emplace(dynamicTextUpdateCoroutine()); // 调用协程函数获取 CoroTask
    mUpdateTask->launch(ll::thread::ServerThreadExecutor::getDefault());
    logger.debug("动态悬浮字更新循环已启动。");
}

void FloatingTextManager::stopDynamicTextUpdateLoop() {
    mShouldStopUpdate.store(true);
    mUpdateTask.reset();
    logger.debug("动态悬浮字更新循环已停止。");
}

// 更新商店/回收商店的悬浮字物品列表
void FloatingTextManager::updateShopFloatingText(BlockPos pos, int dimId, ChestType type) {
    std::unique_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 写锁
    auto key = std::make_pair(dimId, pos);
    if (mFloatingTexts.count(key)) {
        auto& ft = mFloatingTexts.at(key);
        logger.debug(
            "updateShopFloatingText: Updating floating text for chest ({}, {}, {}) in dim {}. Type: {}.",
            pos.x,
            pos.y,
            pos.z,
            dimId,
            static_cast<int>(type)
        );

        if (type != ChestType::Shop && type != ChestType::RecycleShop) {
            logger.warn("updateShopFloatingText: 尝试更新非商店/回收商店类型的悬浮字物品列表，操作无效。");
            return;
        }

        ft.itemNames.clear();                            // 清空旧的物品名称列表
        ft.items.clear();                                // 清空旧的物品列表
        ft.isDynamic                             = true; // 确保标记为动态悬浮字
        Sqlite3Wrapper&                       db = Sqlite3Wrapper::getInstance();
        std::vector<std::vector<std::string>> itemResults;

        if (type == ChestType::Shop) {
            itemResults = db.query(
                "SELECT id.item_nbt FROM shop_items si JOIN item_definitions id ON si.item_id = id.item_id WHERE "
                "si.dim_id = ? AND si.pos_x = ? AND si.pos_y = ? AND si.pos_z = ?;",
                dimId,
                pos.x,
                pos.y,
                pos.z
            );
        } else { // RecycleShop
            itemResults = db.query(
                "SELECT id.item_nbt FROM recycle_shop_items rsi JOIN item_definitions id ON rsi.item_id = id.item_id "
                "WHERE rsi.dim_id = ? AND rsi.pos_x = ? AND rsi.pos_y = ? AND rsi.pos_z = ?;",
                dimId,
                pos.x,
                pos.y,
                pos.z
            );
        }
        logger.debug("updateShopFloatingText: Database query for items returned {} results.", itemResults.size());

        for (const auto& itemRow : itemResults) {
            if (!itemRow.empty()) {
                logger.debug("updateShopFloatingText: Processing item NBT string: {}", itemRow[0]);
                auto nbt = CT::NbtUtils::parseSNBT(itemRow[0]);
                if (nbt) {
                    nbt->at("Count") = ByteTag(1);
                    auto itemPtr     = CT::NbtUtils::createItemFromNbt(*nbt);
                    if (itemPtr && !itemPtr->isNull()) {
                        std::string itemName = itemPtr->getName();
                        if (itemName.empty()) {
                            itemName = itemPtr->getTypeName();
                            logger.warn(
                                "updateShopFloatingText: item.getName() 返回空，使用 item.getTypeName() 作为备用: {}",
                                itemName
                            );
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
        logger.debug(
            "updateShopFloatingText: ft.itemNames contains {} items after database query.",
            ft.itemNames.size()
        );

        // 更新悬浮字文本
        if (!ft.itemNames.empty()) {
            ft.currentItemIndex = 0; // 重置索引
            ft.text             = TextService::getInstance().generateDynamicShopText(type, ft.itemNames[0]);
            logger.debug(
                "updateShopFloatingText: 箱子 ({}, {}, {}) in dim {} 的动态悬浮字已更新为: {}",
                pos.x,
                pos.y,
                pos.z,
                dimId,
                ft.text
            );
        } else {
            ft.text = TextService::getInstance().generateEmptyShopText(type);
            logger.debug(
                "updateShopFloatingText: 箱子 ({}, {}, {}) in dim {} 的动态悬浮字已更新为: {}",
                pos.x,
                pos.y,
                pos.z,
                dimId,
                ft.text
            );
        }

        if (ft.debugText) {
            ft.debugText->setText(ft.text);
            ft.debugText->update(); // 立即更新所有客户端
        }

        // 立即更新所有玩家的假物品
        updateFakeItemsForAllPlayers();
    } else {
        logger.warn(
            "updateShopFloatingText: 尝试更新不存在的悬浮字 ({}, {}, {}) in dim {}。",
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
    }
}


// 发送假物品给玩家
void FloatingTextManager::sendFakeItemToPlayer(Player& player, ChestFloatingText& ft) {
    // 检查全局配置和单箱子配置
    if (ft.items.empty() || !ft.isDynamic || !CT::ConfigManager::getInstance().get().floatingText.enableFakeItem
        || !ft.enableFakeItem)
        return;

    // 边界检查
    if (ft.currentFakeItemIndex >= ft.items.size()) {
        ft.currentFakeItemIndex = 0;
    }

    std::string playerUuid = player.getUuid().asString();

    // 先移除旧的假物品
    removeFakeItemFromPlayer(player, ft);

    // 发送新的假物品（使用常量定义的位置偏移）
    Vec3 itemPos(
        static_cast<float>(ft.pos.x) + FloatingTextConstants::HORIZONTAL_OFFSET,
        static_cast<float>(ft.pos.y) + FloatingTextConstants::ITEM_HEIGHT_OFFSET,
        static_cast<float>(ft.pos.z) + FloatingTextConstants::HORIZONTAL_OFFSET
    );

    auto& currentItem                = ft.items[ft.currentFakeItemIndex];
    auto  id                         = AddFakeitem(itemPos, player, player.getDimensionBlockSource(), currentItem);
    ft.playerFakeItemIds[playerUuid] = id;
}

// 移除玩家的假物品
void FloatingTextManager::removeFakeItemFromPlayer(Player& player, ChestFloatingText& ft) {
    std::string playerUuid = player.getUuid().asString();
    auto        it         = ft.playerFakeItemIds.find(playerUuid);
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

    // 优化：先收集所有需要更新的悬浮字数据，按维度分组，避免在遍历玩家时持锁
    std::unordered_map<int, std::vector<std::pair<int, BlockPos>>> fakeItemsByDim;

    {
        std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex); // 读锁
        for (const auto& [key, ft] : mFloatingTexts) {
            if (ft.isDynamic && !ft.items.empty()) {
                fakeItemsByDim[ft.dimId].emplace_back(key);
            }
        }
    } // 快速释放锁

    // 遍历玩家时不再需要持锁
    level->forEachPlayer([this, &fakeItemsByDim](Player& player) {
        int playerDimId = player.getDimensionId().id;

        // 检查该维度是否有假物品需要更新
        auto dimIt = fakeItemsByDim.find(playerDimId);
        if (dimIt == fakeItemsByDim.end()) return true;

        // 获取读锁，批量处理该维度的所有假物品
        std::shared_lock<std::shared_mutex> lock(mFloatingTextsMutex);
        for (const auto& key : dimIt->second) {
            auto it = mFloatingTexts.find(key);
            if (it == mFloatingTexts.end()) continue; // 可能在锁释放期间被删除

            auto& ft = it->second;
            // 再次检查条件（可能在锁释放期间被修改）
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
                FloatingTextManager::getInstance().loadAllChests();
            }

            FloatingTextManager::getInstance().drawAllFloatingTexts(player);

            // 延迟发送假物品，等待玩家完全初始化
            if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
                std::string playerUuid = player.getUuid().asString();
                ll::thread::ServerThreadExecutor::getDefault().execute([playerUuid]() {
                    auto level = ll::service::getLevel();
                    if (!level) return;
                    auto* playerPtr = level->getPlayer(playerUuid);
                    if (!playerPtr) return;
                    int playerDimId = playerPtr->getDimensionId().id;

                    // 优化：先收集该维度的所有动态悬浮字，减少锁持有时间
                    std::vector<std::pair<int, BlockPos>> keysToProcess;
                    {
                        auto&                               manager = FloatingTextManager::getInstance();
                        std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
                        for (const auto& [key, ft] : manager.mFloatingTexts) {
                            if (ft.isDynamic && !ft.items.empty() && ft.dimId == playerDimId) {
                                keysToProcess.push_back(key);
                            }
                        }
                    } // 快速释放锁

                    // 批量处理假物品发送
                    if (!keysToProcess.empty()) {
                        auto& manager = FloatingTextManager::getInstance();
                        std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
                        for (const auto& key : keysToProcess) {
                            auto it = manager.mFloatingTexts.find(key);
                            if (it != manager.mFloatingTexts.end()) {
                                manager.sendFakeItemToPlayer(*playerPtr, it->second);
                            }
                        }
                    }
                    logger.debug("玩家 {} 加入游戏，已为其绘制所有悬浮字和假物品。", playerPtr->getRealName());
                });
            } else {
                logger.debug("玩家 {} 加入游戏，已为其绘制所有悬浮字。", player.getRealName());
            }
        }
    );
}


// 玩家跨维度
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

    // 优化：先收集需要移除假物品的键，减少锁持有时间
    if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
        std::vector<std::pair<int, BlockPos>> keysToRemove;
        {
            auto&                               manager = FloatingTextManager::getInstance();
            std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
            for (const auto& [key, ft] : manager.mFloatingTexts) {
                if (ft.isDynamic && ft.dimId == static_cast<int>(fromDim)) {
                    keysToRemove.push_back(key);
                }
            }
        } // 快速释放锁

        // 批量移除旧维度的假物品
        if (!keysToRemove.empty()) {
            auto& manager = FloatingTextManager::getInstance();
            std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
            for (const auto& key : keysToRemove) {
                auto it = manager.mFloatingTexts.find(key);
                if (it != manager.mFloatingTexts.end()) {
                    manager.removeFakeItemFromPlayer(player, it->second);
                }
            }
        }
    }

    // 在玩家切换维度之前调用原始函数
    origin(player, std::move(changeRequest));

    // 切换维度后，更新悬浮字和假物品
    std::string playerUuid = player.getUuid().asString();
    ll::thread::ServerThreadExecutor::getDefault().execute([playerUuid, fromDim, toDim]() {
        auto level = ll::service::getLevel();
        if (!level) return;
        auto* playerPtr = level->getPlayer(playerUuid);
        if (!playerPtr) return;

        FloatingTextManager::getInstance().removeAllFloatingTexts(*playerPtr, fromDim);
        FloatingTextManager::getInstance().drawAllFloatingTexts(*playerPtr, toDim);

        // 发送新维度的假物品
        if (CT::ConfigManager::getInstance().get().floatingText.enableFakeItem) {
            // 优化：先收集新维度的所有动态悬浮字
            std::vector<std::pair<int, BlockPos>> keysToAdd;
            {
                auto&                               manager = FloatingTextManager::getInstance();
                std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
                for (const auto& [key, ft] : manager.mFloatingTexts) {
                    if (ft.isDynamic && !ft.items.empty() && ft.dimId == static_cast<int>(toDim)) {
                        keysToAdd.push_back(key);
                    }
                }
            } // 快速释放锁

            // 批量发送新维度的假物品
            if (!keysToAdd.empty()) {
                auto& manager = FloatingTextManager::getInstance();
                std::shared_lock<std::shared_mutex> lock(manager.mFloatingTextsMutex);
                for (const auto& key : keysToAdd) {
                    auto it = manager.mFloatingTexts.find(key);
                    if (it != manager.mFloatingTexts.end()) {
                        manager.sendFakeItemToPlayer(*playerPtr, it->second);
                    }
                }
            }
        }
    });
}
} // namespace CT
