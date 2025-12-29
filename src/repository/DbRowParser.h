#pragma once

#include "logger.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace CT {

/**
 * @brief 数据库行解析器，封装通用的数据解析逻辑
 */
class DbRowParser {
public:
    using Row = std::vector<std::string>;

    explicit DbRowParser(const Row& row) : row_(row) {}

    bool hasColumns(size_t count) const { return row_.size() >= count; }

    int         getInt(size_t index) const { return std::stoi(row_.at(index)); }
    double      getDouble(size_t index) const { return std::stod(row_.at(index)); }
    bool        getBool(size_t index) const { return std::stoi(row_.at(index)) != 0; }
    std::string getString(size_t index) const { return row_.at(index); }

    int getIntOr(size_t index, int defaultVal) const {
        return index < row_.size() ? std::stoi(row_[index]) : defaultVal;
    }

private:
    const Row& row_;
};

/**
 * @brief 解析单行结果
 */
template <typename T, typename Parser>
std::optional<T> parseSingleRow(const std::vector<std::vector<std::string>>& results, size_t minCols, Parser parser) {
    if (results.empty() || results[0].size() < minCols) {
        return std::nullopt;
    }
    return parser(DbRowParser(results[0]));
}

/**
 * @brief 解析多行结果
 */
template <typename T, typename Parser>
std::vector<T> parseRows(const std::vector<std::vector<std::string>>& results, size_t minCols, Parser parser) {
    std::vector<T> items;
    for (const auto& row : results) {
        if (row.size() >= minCols) {
            try {
                items.push_back(parser(DbRowParser(row)));
            } catch (const std::exception& e) {
                logger.error("DbRowParser: Failed to parse row: {}", e.what());
            } catch (...) {
                logger.error("DbRowParser: Failed to parse row: unknown exception");
            }
        }
    }
    return items;
}

} // namespace CT
