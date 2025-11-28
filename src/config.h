#pragma once

#include "Config/JsonMacros.h"
#include "ll/api/io/LogLevel.h"


namespace CT { // 将 EconomyType 定义在 CT 命名空间下

enum class EconomyType { LLMoney = 0, CzMoney = 1 };

} // namespace CT

struct FloatingTextSettings {
    bool enableLockedChest   = true; // 是否开启上锁箱子的悬浮字显示
    bool enablePublicChest   = true; // 是否开启公共箱子的悬浮字显示
    bool enableRecycleShop   = true; // 是否开启回收商店的悬浮字显示
    bool enableShopChest     = true; // 是否开启商店箱子的悬浮字显示
    bool enableFakeItem      = true; // 是否开启假掉落物显示
};

struct Config {
    int                   version                           = 1;
    bool                  enableWalMode                     = true;       // 是否启用数据库的 WAL 模式
    int                   floatingTextUpdateIntervalSeconds = 1;          // 悬浮字更新间隔，单位秒
    int                   databaseThreadPoolSize            = 4;          // 数据库线程池线程数量
    ll::io::LogLevel      logLevel = ll::io::LogLevel::Info; // 日志等级
    CT::EconomyType       economyType = CT::EconomyType::CzMoney; // 经济类型，默认 LLMoney
    FloatingTextSettings floatingText;                            // 悬浮字相关设置
};
