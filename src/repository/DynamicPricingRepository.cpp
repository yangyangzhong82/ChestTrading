#include "DynamicPricingRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <ctime>

namespace CT {

DynamicPricingRepository& DynamicPricingRepository::getInstance() {
    static DynamicPricingRepository instance;
    return instance;
}

std::string DynamicPricingRepository::serializeTiers(const std::vector<PriceTier>& tiers) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tiers) {
        arr.push_back({
            {"threshold", t.threshold},
            {"price",     t.price    }
        });
    }
    return arr.dump();
}

std::vector<PriceTier> DynamicPricingRepository::deserializeTiers(const std::string& json) {
    std::vector<PriceTier> tiers;
    try {
        auto arr = nlohmann::json::parse(json);
        for (const auto& item : arr) {
            tiers.push_back({item["threshold"].get<int>(), item["price"].get<double>()});
        }
        // 按阈值降序排序，避免每次计算价格时重复排序
        std::sort(tiers.begin(), tiers.end(), [](const PriceTier& a, const PriceTier& b) {
            return a.threshold > b.threshold;
        });
    } catch (const std::exception& e) {
        logger.error("deserializeTiers failed: {}", e.what());
    }
    return tiers;
}

bool DynamicPricingRepository::upsert(const DynamicPricingData& data) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT OR REPLACE INTO dynamic_pricing "
        "(dim_id, pos_x, pos_y, pos_z, item_id, is_shop, price_tiers, stop_threshold, "
        "current_count, reset_interval_hours, last_reset_time, enabled) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        data.dimId,
        data.pos.x,
        data.pos.y,
        data.pos.z,
        data.itemId,
        data.isShop ? 1 : 0,
        serializeTiers(data.priceTiers),
        data.stopThreshold,
        data.currentCount,
        data.resetIntervalHours,
        data.lastResetTime,
        data.enabled ? 1 : 0
    );
}

bool DynamicPricingRepository::remove(BlockPos pos, int dimId, int itemId, bool isShop) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM dynamic_pricing WHERE dim_id=? AND pos_x=? AND pos_y=? AND pos_z=? AND item_id=? AND is_shop=?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId,
        isShop ? 1 : 0
    );
}

bool DynamicPricingRepository::removeAll(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM dynamic_pricing WHERE dim_id=? AND pos_x=? AND pos_y=? AND pos_z=?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

std::optional<DynamicPricingData> DynamicPricingRepository::find(BlockPos pos, int dimId, int itemId, bool isShop) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, enabled "
         "FROM dynamic_pricing WHERE dim_id=? AND pos_x=? AND pos_y=? AND pos_z=? AND item_id=? AND is_shop=?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId,
        isShop ? 1 : 0
    );

    return parseSingleRow<DynamicPricingData>(results, 6, [&](DbRowParser r) {
        return DynamicPricingData{
            dimId,
            pos,
            itemId,
            isShop,
            deserializeTiers(r.getString(0)),
            r.getInt(1),
            r.getInt(2),
            r.getInt(3),
            std::stoll(r.getString(4)),
            r.getBool(5)
        };
    });
}

std::vector<DynamicPricingData> DynamicPricingRepository::findAll(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, "
         "enabled FROM dynamic_pricing WHERE dim_id=? AND pos_x=? AND pos_y=? AND pos_z=?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    return parseRows<DynamicPricingData>(results, 8, [&](DbRowParser r) {
        return DynamicPricingData{
            dimId,
            pos,
            r.getInt(0),
            r.getBool(1),
            deserializeTiers(r.getString(2)),
            r.getInt(3),
            r.getInt(4),
            r.getInt(5),
            std::stoll(r.getString(6)),
            r.getBool(7)
        };
    });
}

bool DynamicPricingRepository::incrementCount(BlockPos pos, int dimId, int itemId, bool isShop, int amount) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE dynamic_pricing SET current_count = current_count + ? "
        "WHERE dim_id=? AND pos_x=? AND pos_y=? AND pos_z=? AND item_id=? AND is_shop=?",
        amount,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId,
        isShop ? 1 : 0
    );
}

int DynamicPricingRepository::resetExpiredCounters() {
    auto&   db  = Sqlite3Wrapper::getInstance();
    int64_t now = std::time(nullptr);

    return db.executeAndGetChanges(
        "UPDATE dynamic_pricing SET current_count = 0, last_reset_time = ? "
        "WHERE (? - last_reset_time) >= (reset_interval_hours * 3600)",
        now,
        now
    );
}

} // namespace CT
