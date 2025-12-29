#include "ItemRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"

namespace CT {

ItemRepository& ItemRepository::getInstance() {
    static ItemRepository instance;
    return instance;
}

int ItemRepository::getOrCreateItemId(const std::string& itemNbt) {
    auto& db = Sqlite3Wrapper::getInstance();

    auto results = db.query("SELECT item_id FROM item_definitions WHERE item_nbt = ?;", itemNbt);
    if (auto id = parseSingleRow<int>(results, 1, [](DbRowParser r) { return r.getInt(0); })) {
        return *id;
    }

    if (db.execute("INSERT INTO item_definitions (item_nbt) VALUES (?);", itemNbt)) {
        auto idResults = db.query("SELECT last_insert_rowid();");
        if (auto id = parseSingleRow<int>(idResults, 1, [](DbRowParser r) { return r.getInt(0); })) {
            return *id;
        }
    }

    logger.error("ItemRepository: Failed to get or create item_id for NBT");
    return -1;
}

std::optional<std::string> ItemRepository::getItemNbtById(int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT item_nbt FROM item_definitions WHERE item_id = ?;", itemId);
    return parseSingleRow<std::string>(results, 1, [](DbRowParser r) { return r.getString(0); });
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