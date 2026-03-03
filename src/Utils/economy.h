#pragma once

#if CT_ENABLE_CZMONEY
#include "czmoney/money_api.h"
#endif
#include "mc/world/actor/player/Player.h"
#include <string>


namespace CT {
namespace Economy {

// ============================================================================
// 货币系统说明
// ============================================================================
// 本系统支持两种经济插件：LLMoney 和 CzMoney
//
// 货币精度策略：
// - CzMoney: 支持小数货币（double 类型）
// - LLMoney: 仅支持整数货币（int 类型）
//
// LLMoney 舍入规则：
// - 统一采用"向下取整"策略（std::floor）
// - 小数部分直接舍去，例如：
//   * 0.9 -> 0（无法扣除或添加）
//   * 1.1 -> 1
//   * 5.99 -> 5
// - 当转换结果 <= 0 时，操作视为成功但不实际修改余额
//
// 注意事项：
// - 使用 LLMoney 时，请确保传入的金额为整数或可接受向下取整
// - 避免使用小于 1 的金额进行 LLMoney 操作（将被舍去为 0）
// ============================================================================

/**
 * @brief 检查玩家是否有足够的金币
 * @param player 玩家对象
 * @param amount 金额（LLMoney 会向下取整）
 * @return 是否有足够金币
 */
bool hasMoney(Player& player, double amount);

/**
 * @brief 扣除玩家金币
 * @param player 玩家对象
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不扣除）
 * @return 是否扣除成功
 */
bool reduceMoney(Player& player, double amount);

/**
 * @brief 增加玩家金币
 * @param player 玩家对象
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不添加）
 * @return 是否添加成功
 */
bool addMoney(Player& player, double amount);

/**
 * @brief 通过 XUID 增加玩家金币
 * @param xuid 玩家 XUID
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不添加）
 * @return 是否添加成功
 */
bool addMoneyByXuid(const std::string& xuid, double amount);

/**
 * @brief 通过 UUID 增加玩家金币
 * @param uuid 玩家 UUID
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不添加）
 * @return 是否添加成功
 */
bool addMoneyByUuid(const std::string& uuid, double amount);

/**
 * @brief 获取玩家金币数量
 * @param player 玩家对象
 * @return 金币数量（LLMoney 返回整数，CzMoney 可能包含小数）
 */
double getMoney(Player& player);

/**
 * @brief 通过 XUID 获取玩家金币数量
 * @param xuid 玩家 XUID
 * @return 金币数量（LLMoney 返回整数，CzMoney 可能包含小数）
 */
double getMoney(const std::string& xuid);

/**
 * @brief 通过 XUID 扣除玩家金币
 * @param xuid 玩家 XUID
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不扣除）
 * @return 是否扣除成功
 */
bool reduceMoneyByXuid(const std::string& xuid, double amount);

/**
 * @brief 通过 UUID 扣除玩家金币
 * @param uuid 玩家 UUID
 * @param amount 金额（LLMoney 会向下取整，<= 0 时视为成功但不扣除）
 * @return 是否扣除成功
 */
bool reduceMoneyByUuid(const std::string& uuid, double amount);

} // namespace Economy
} // namespace CT
