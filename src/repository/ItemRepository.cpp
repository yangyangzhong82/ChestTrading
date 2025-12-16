#include "ItemRepository.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"

namespace CT {

ItemRepository& ItemRepository::getInstance() {
    static ItemRepository instance;
    return instance;
}

int ItemRepository::getOrCreateItemId(const std::string& itemNbt) {
    auto& db = Sqlite3Wrapper::getInstance();

    // 先查找是否已存在
    auto results = db.query("SELECT item_id FROM item_definitions WHERE item_nbt = ?;", itemNbt);
    if (!results.empty() && !results[0].empty()) {
        return std::stoi(results[0][0]);
    }

    // 不存在则插入
    if (db.execute("INSERT INTO item_definitions (item_nbt) VALUES (?);", itemNbt)) {
        // 获取刚插入的ID
        auto idResults = db.query("SELECT last_insert_rowid();");
        if (!idResults.empty() && !idResults[0].empty()) {
            return std::stoi(idResults[0][0]);
        }
    }

    logger.error("ItemRepository: Failed to get or create item_id for NBT");
    return -1;
}

std::optional<std::string> ItemRepository::getItemNbtById(int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT item_nbt FROM item_definitions WHERE item_id = ?;", itemId);

    if (!results.empty() && !results[0].empty()) {
        return results[0][0];
    }
    return std::nullopt;
}

bool ItemRepository::exists(int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT 1 FROM item_definitions WHERE item_id = ?;", itemId);
    return !results.empty();
}

std::string ItemRepository::getItemNbt(int itemId) {
    auto result = getItemNbtById(itemId);
    return result.value_or("");
}

} // namespace CT