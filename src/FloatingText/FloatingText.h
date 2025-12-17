#pragma once

#include "Types.h" // ChestType 定义
#include "debug_shape/api/IDebugShapeDrawer.h"
#include "debug_shape/api/shape/IDebugText.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/coro/SleepAwaiter.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/dimension/Dimension.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace CT {
// 存储每个箱子的悬浮字信息
struct ChestFloatingText {
    BlockPos                                 pos;
    int                                      dimId;
    std::string                              ownerUuid;
    std::string                              text;
    std::unique_ptr<debug_shape::IDebugText> debugText; // 对应的 DebugText 对象
    ChestType                                type;
    bool                                     isDynamic      = false;
    bool                                     enableFakeItem = true; // 单箱子假物品配置
    std::vector<std::string>                 itemNames;
    size_t                                   currentItemIndex     = 0;
    size_t                                   currentFakeItemIndex = 0; // 假物品独立索引

    // 假物品相关
    std::vector<ItemStack>               items;             // 存储物品
    std::map<std::string, ActorUniqueID> playerFakeItemIds; // 玩家UUID -> 假物品ID

    // 构造函数
    ChestFloatingText(BlockPos p, int d, std::string uuid, std::string t, ChestType ct, bool fakeItem = true)
    : pos(p),
      dimId(d),
      ownerUuid(std::move(uuid)),
      text(std::move(t)),
      type(ct),
      enableFakeItem(fakeItem) {}
};

// 悬浮字管理器
class FloatingTextManager {
public:
    // 使用 map 存储悬浮字，键为 (dimId, BlockPos)
    std::map<std::pair<int, BlockPos>, ChestFloatingText> mFloatingTexts;
    std::optional<ll::coro::CoroTask<>>                   mUpdateTask; // 用于更新悬浮字的协程任务
    bool              mIsLoaded = false;        // 标志，指示是否已从数据库加载悬浮字
    std::atomic<bool> mShouldStopUpdate{false}; // 控制协程停止

    FloatingTextManager() = default; // 私有构造函数，实现单例模式

    void startDynamicTextUpdateLoop(); // 启动动态更新循环
    void stopDynamicTextUpdateLoop();  // 停止动态更新循环

private:
    ll::coro::CoroTask<> dynamicTextUpdateCoroutine(); // 新增协程函数声明

public:
    // 获取单例实例
    static FloatingTextManager& getInstance();
    // 添加或更新一个箱子的悬浮字
    void addOrUpdateFloatingText(
        BlockPos           pos,
        int                dimId,
        const std::string& ownerUuid,
        const std::string& text,
        ChestType          type
    );

    // 移除一个箱子的悬浮字
    void removeFloatingText(BlockPos pos, int dimId);

    // 绘制所有悬浮字给特定玩家
    void drawAllFloatingTexts(Player& player);

    // 移除所有悬浮字给特定玩家
    void removeAllFloatingTexts(Player& player);

    // 为特定玩家移除特定维度的所有悬浮字
    void removeAllFloatingTexts(Player& player, DimensionType dimension);

    // 为特定玩家绘制特定维度的所有悬浮字
    void drawAllFloatingTexts(Player& player, DimensionType dimension);

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

    // 更新商店/回收商店的悬浮字物品列表
    void updateShopFloatingText(BlockPos pos, int dimId, ChestType type);

    // 假物品相关方法
    void sendFakeItemToPlayer(Player& player, ChestFloatingText& ft);
    void removeFakeItemFromPlayer(Player& player, ChestFloatingText& ft);
    void updateFakeItemsForAllPlayers();
};

void registerPlayerConnectionListener();

} // namespace CT
