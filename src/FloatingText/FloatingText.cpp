#include "FloatingText/FloatingText.h"
#include "Utils/NetworkPacket.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "debug_shape/api/IDebugShapeDrawer.h"
#include "debug_shape/api/shape/IDebugText.h"
#include "interaction/chestprotect.h" // 引入 getChestDetails
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
#include "mc/world/actor/DataItem.h" // 添加缺失的头文件
#include "mc/nbt/CompoundTag.h"     // 添加缺失的头文件
#include "ll/api/base/Meta.h"       // 添加缺失的头文件
#include "Utils/NetworkPacket.h"


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
    const std::string& text
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
    } else {
        // 创建新的悬浮字
        auto newText = debug_shape::IDebugText::create(
            Vec3(static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 1.5f, static_cast<float>(pos.z) + 0.5f),
            text
        );
        mFloatingTexts.emplace(key, ChestFloatingText(pos, dimId, ownerUuid, text));
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
            addOrUpdateFloatingText(pos, dimId, ownerUuid, text);
        }
    }
    mIsLoaded = true; // 设置加载标志为 true
    logger.debug("已从数据库加载 {} 个已锁定箱子的悬浮字。", mFloatingTexts.size());
}


void registerPlayerConnectionListener() {
    ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerJoinEvent>(
        [](ll::event::player::PlayerJoinEvent& event) {
            auto& player = event.self();
            // 在第一个玩家加入时加载所有悬浮字
            if (!FloatingTextManager::getInstance().mIsLoaded) {
                FloatingTextManager::getInstance().loadAllLockedChests();
            }

            //
            FloatingTextManager::getInstance()
                .drawAllFloatingTexts(player);
            logger.debug("玩家 {} 加入游戏，已为其绘制所有悬浮字。", player.getRealName());
        }
    );
}


}
