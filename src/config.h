#pragma once

#include "Config/JsonMacros.h"
#include "ll/api/io/LogLevel.h"


namespace CT { // 将 EconomyType 定义在 CT 命名空间下

enum class EconomyType { LLMoney = 0, CzMoney = 1 };

} // namespace CT

struct FloatingTextSettings {
    bool enableLockedChest = true; // 是否开启上锁箱子的悬浮字显示
    bool enablePublicChest = true; // 是否开启公共箱子的悬浮字显示
    bool enableRecycleShop = true; // 是否开启回收商店的悬浮字显示
    bool enableShopChest   = true; // 是否开启商店箱子的悬浮字显示
    bool enableFakeItem    = true; // 是否开启假掉落物显示
};

struct ChestLimits {
    int maxLockedChests = 10; // 每名玩家最多可创建的上锁箱子数量
    int maxPublicChests = 5;  // 每名玩家最多可创建的公共箱子数量
    int maxRecycleShops = 3;  // 每名玩家最多可创建的回收商店数量
    int maxShops        = 3;  // 每名玩家最多可创建的商店数量
};

struct ChestCreationCosts {
    double lockedChestCost = 100.0; // 创建上锁箱子的费用
    double publicChestCost = 50.0;  // 创建公共箱子的费用
    double recycleShopCost = 500.0; // 创建回收商店的费用
    double shopCost        = 500.0; // 创建商店的费用
};

struct Config {
    int  version       = 1;
    bool enableWalMode = true; // 是否启用数据库的 WAL 模式
    int  busyTimeoutMs = 5000; // 数据库繁忙超时时间（毫秒），避免 "database is locked" 错误
    int  floatingTextUpdateIntervalSeconds = 1;                        // 悬浮字更新间隔，单位秒
    int  databaseThreadPoolSize            = 4;                        // 数据库线程池线程数量
    ll::io::LogLevel     logLevel          = ll::io::LogLevel::Info;   // 日志等级
    CT::EconomyType      economyType       = CT::EconomyType::CzMoney; // 经济类型
    FloatingTextSettings floatingText;                                 // 悬浮字相关设置
    ChestLimits          chestLimits;                                  // 箱子数量限制
    ChestCreationCosts   chestCosts;                                   // 箱子创建费用
};
