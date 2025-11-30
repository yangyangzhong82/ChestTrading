#pragma once

#include <string>
#include <sstream>
#include <iomanip>

namespace CT {
namespace MoneyFormat {

/**
 * @brief 格式化金钱数值为保留两位小数的字符串
 * @param amount 金钱数值
 * @return 格式化后的字符串（保留两位小数）
 */
inline std::string format(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount;
    return oss.str();
}

} // namespace MoneyFormat
} // namespace CT
