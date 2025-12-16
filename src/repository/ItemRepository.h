#pragma once

#include <optional>
#include <string>

namespace CT {

/**
 * @brief 物品定义数据访问层
 * 管理 item_definitions 表
 */
class ItemRepository {
public:
    static ItemRepository& getInstance();

    ItemRepository(const ItemRepository&)            = delete;
    ItemRepository& operator=(const ItemRepository&) = delete;

    // 获取或创建物品ID
    int getOrCreateItemId(const std::string& itemNbt);

    // 根据ID获取物品NBT
    std::optional<std::string> getItemNbtById(int itemId);

    // 便捷方法：获取物品NBT，返回空字符串如果不存在
    std::string getItemNbt(int itemId);

    // 检查物品是否存在
    bool exists(int itemId);

private:
    ItemRepository() = default;
};

} // namespace CT