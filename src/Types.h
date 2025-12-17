#pragma once

namespace CT {

/**
 * @brief 箱子类型枚举
 * 定义所有支持的箱子类型
 */
enum class ChestType {
    Locked       = 0, // 普通上锁箱子
    Public       = 1, // 公共箱子
    Shop         = 2, // 商店箱子
    RecycleShop  = 3, // 回收商店
    Invalid      = 4, // 无效或未定义
    AdminShop    = 5, // 管理员商店
    AdminRecycle = 6  // 管理员回收商店
};

} // namespace CT