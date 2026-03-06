#pragma once

#include "Config/JsonMacros.h"
#include "ll/api/io/LogLevel.h"

#include <string>
#include <vector>


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

struct TeleportSettings {
    double teleportCost        = 100.0; // 传送到商店的费用
    int    teleportCooldownSec = 60;    // 传送冷却时间（秒）
};

struct TaxSettings {
    double shopTaxRate    = 0.0; // 商店交易税率 (0.0 - 1.0, 例如 0.05 表示 5%)
    double recycleTaxRate = 0.0; // 回收商店税率 (0.0 - 1.0, 例如 0.05 表示 5%)
};

struct ResourcePaths {
    std::string              databasePath     = "plugins/ChestTrading/data/ChestTrading.db"; // 数据库文件路径
    std::vector<std::string> itemTextureFiles = {
        "plugins/ChestTrading/icon/texture_path.json",
        "plugins/ChestTrading/icon/terrain_texture.json",
        "plugins/ChestTrading/icon/item_texture.json"
    }; // 物品贴图文件，按优先级排序，先加载的优先
    std::string languageDir = "plugins/ChestTrading/lang"; // 语言文件目录
    std::string language    = "zh_CN";                     // 当前语言，支持 zh_CN, en_US
};

struct InteractionSettings {
    int         debounceIntervalMs  = 500;               // 箱子交互防抖间隔（毫秒）
    int         cleanupThresholdSec = 60;                // 交互记录清理阈值（秒）
    std::string manageToolItem      = "minecraft:stick"; // 触发箱子管理的物品ID（留空=禁用物品触发）
    bool        requireSneakingForManage = false;        // 触发箱子管理时是否必须下蹲
};

struct SalesRankingSettings {
    int maxDisplayCount = 50; // 销量榜单最多显示数量
};

struct ShopNameRestrictions {
    int                      maxLength       = 32; // 商店名称最大长度，按 UTF-8 字符数计算，<=0 表示不限制
    std::vector<std::string> blockedKeywords = {}; // 禁止出现在商店名称中的关键词，按子串匹配
};

struct Config {
    int  version       = 1;
    bool enableWalMode = true; // 是否启用数据库的 WAL 模式
    int  busyTimeoutMs = 5000; // 数据库繁忙超时时间（毫秒），避免 "database is locked" 错误
    int  floatingTextUpdateIntervalSeconds = 1;                        // 悬浮字更新间隔，单位秒
    int  databaseThreadPoolSize            = 4;                        // 数据库线程池线程数量
    int  databaseCacheTimeoutSec           = 60;                       // 数据库查询缓存超时时间（秒）
    ll::io::LogLevel     logLevel          = ll::io::LogLevel::Info;   // 日志等级
#if CT_ENABLE_CZMONEY
    CT::EconomyType      economyType       = CT::EconomyType::CzMoney; // 经济类型
#else
    CT::EconomyType      economyType       = CT::EconomyType::LLMoney; // 经济类型
#endif
    FloatingTextSettings floatingText;                                 // 悬浮字相关设置
    ChestLimits          chestLimits;                                  // 箱子数量限制
    ChestCreationCosts   chestCosts;                                   // 箱子创建费用
    TeleportSettings     teleportSettings;                             // 传送相关设置
    TaxSettings          taxSettings;                                  // 税率设置
    ResourcePaths        resourcePaths;                                // 数据库/贴图等路径配置
    InteractionSettings  interactionSettings;                          // 交互相关设置
    SalesRankingSettings salesRankingSettings;                         // 销量榜单设置
    ShopNameRestrictions shopNameRestrictions;                         // 商店名称限制
};
