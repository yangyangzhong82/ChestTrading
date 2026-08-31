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

    // 获取或创建物品ID（写路径专用：会向 item_definitions 插入新行）
    int getOrCreateItemId(const std::string& itemNbt);

    // 只查不建：用于纯展示路径，避免仅仅打开界面就往 item_definitions 里塞行。
    // 对带大 NBT 的物品（例如装了蜜蜂的蜂巢，每个都是唯一的）尤其重要，
    // 否则 item_nbt 这个 UNIQUE 索引会被撑大，拖慢后续所有查找。
    std::optional<int> findItemIdByNbt(const std::string& itemNbt);

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