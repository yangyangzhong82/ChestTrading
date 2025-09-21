#pragma once

#include "debug_shape/DebugText.h"
#include "ll/api/service/PlayerInfo.h" // 引入 PlayerInfo
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/Dimension.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CT {

// 存储每个箱子的悬浮字信息
struct ChestFloatingText {
    BlockPos                       pos;
    int                            dimId;
    std::string                    ownerUuid;
    std::string                    text;
    std::shared_ptr<debug_shape::DebugText> debugText; // 对应的 DebugText 对象

    // 构造函数
    ChestFloatingText(BlockPos p, int d, std::string uuid, std::string t)
        : pos(p), dimId(d), ownerUuid(std::move(uuid)), text(std::move(t)) {}
};

// 悬浮字管理器
class FloatingTextManager {
private:
    // 使用 map 存储悬浮字，键为 (dimId, BlockPos)
    std::map<std::pair<int, BlockPos>, ChestFloatingText> mFloatingTexts;


    FloatingTextManager() = default; // 私有构造函数，实现单例模式

public:
    // 获取单例实例
    static FloatingTextManager& getInstance();
    bool                        mIsLoaded = false; // 标志，指示是否已从数据库加载悬浮字
    // 添加或更新一个箱子的悬浮字
    void addOrUpdateFloatingText(BlockPos pos, int dimId, const std::string& ownerUuid, const std::string& text);

    // 移除一个箱子的悬浮字
    void removeFloatingText(BlockPos pos, int dimId);

    // 绘制所有悬浮字给特定玩家
    void drawAllFloatingTexts(Player& player);

    // 移除所有悬浮字给特定玩家
    void removeAllFloatingTexts(Player& player);

    // 绘制所有悬浮字给特定维度
    void drawAllFloatingTexts(DimensionType dimension);

    // 移除所有悬浮字给特定维度
    void removeAllFloatingTexts(DimensionType dimension);

    // 绘制所有悬浮字给所有客户端
    void drawAllFloatingTexts();

    // 移除所有悬浮字给所有客户端
    void removeAllFloatingTexts();

    // 从数据库加载所有已锁定的箱子并创建悬浮字
    void loadAllLockedChests();
};

void registerPlayerConnectionListener();

} // namespace CT
