#pragma once

#include <cstdint>
#include <optional>

namespace CT {

/**
 * @brief PLand 领地箱子设置的数据访问层
 * 按领地 ID 存储"允许其他玩家创建哪些类型的箱子"（位掩码，第 i 位对应 ChestType 枚举值 i）。
 */
class LandSettingRepository {
public:
    static LandSettingRepository& getInstance();

    LandSettingRepository(const LandSettingRepository&)            = delete;
    LandSettingRepository& operator=(const LandSettingRepository&) = delete;

    /**
     * @brief 查询某领地允许其他玩家创建的箱子类型位掩码。
     * @return std::nullopt 表示尚无该领地的记录（调用方按默认值“全部允许”处理）。
     */
    std::optional<int> getAllowedTypesMask(int64_t landId);

    bool setAllowedTypesMask(int64_t landId, int mask);

    bool remove(int64_t landId);

private:
    LandSettingRepository() = default;
};

} // namespace CT
