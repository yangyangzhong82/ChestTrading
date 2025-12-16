#include "ShopRepository.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"

namespace CT {

ShopRepository& ShopRepository::getInstance() {
    static ShopRepository instance;
    return instance;
}

bool ShopRepository::upsertItem(const ShopItemData& item) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_id) DO UPDATE SET "
        "price = excluded.price, db_count = excluded.db_count, slot = excluded.slot;",
        item.dimId,
        item.pos.x,
        item.pos.y,
        item.pos.z,
        item.slot,
        item.itemId,
        item.price,
        item.dbCount
    );
}

bool ShopRepository::removeItem(BlockPos pos, int dimId, int itemId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
}

bool ShopRepository::removeAllItems(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

std::optional<ShopItemData> ShopRepository::findItem(BlockPos pos, int dimId, int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT s.slot, s.price, s.db_count, d.item_nbt FROM shop_items s "
         "JOIN item_definitions d ON s.item_id = d.item_id "
         "WHERE s.dim_id = ? AND s.pos_x = ? AND s.pos_y = ? AND s.pos_z = ? AND s.item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );

    if (results.empty() || results[0].size() < 4) {
        return std::nullopt;
    }

    const auto&  row = results[0];
    ShopItemData data;
    data.dimId   = dimId;
    data.pos     = pos;
    data.itemId  = itemId;
    data.slot    = std::stoi(row[0]);
    data.price   = std::stod(row[1]);
    data.dbCount = std::stoi(row[2]);
    data.itemNbt = row[3];
    return data;
}

std::vector<ShopItemData> ShopRepository::findAllItems(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT s.item_id, s.slot, s.price, s.db_count, d.item_nbt FROM shop_items s "
         "JOIN item_definitions d ON s.item_id = d.item_id "
         "WHERE s.dim_id = ? AND s.pos_x = ? AND s.pos_y = ? AND s.pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    std::vector<ShopItemData> items;
    for (const auto& row : results) {
        if (row.size() >= 5) {
            ShopItemData data;
            data.dimId   = dimId;
            data.pos     = pos;
            data.itemId  = std::stoi(row[0]);
            data.slot    = std::stoi(row[1]);
            data.price   = std::stod(row[2]);
            data.dbCount = std::stoi(row[3]);
            data.itemNbt = row[4];
            items.push_back(data);
        }
    }
    return items;
}

bool ShopRepository::updateDbCount(BlockPos pos, int dimId, int itemId, int newCount) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE shop_items SET db_count = ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = "
        "?;",
        newCount,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
}

bool ShopRepository::decrementDbCount(BlockPos pos, int dimId, int itemId, int amount) {
    auto& db      = Sqlite3Wrapper::getInstance();
    int   changed = db.executeAndGetChanges(
        "UPDATE shop_items SET db_count = db_count - ? "
          "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ? AND db_count >= ?;",
        amount,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId,
        amount
    );
    return changed > 0;
}

bool ShopRepository::addPurchaseRecord(const PurchaseRecordData& record) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO purchase_records (dim_id, pos_x, pos_y, pos_z, item_id, buyer_uuid, purchase_count, total_price) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        record.dimId,
        record.pos.x,
        record.pos.y,
        record.pos.z,
        record.itemId,
        record.buyerUuid,
        record.purchaseCount,
        record.totalPrice
    );
}

std::vector<PurchaseRecordData> ShopRepository::getPurchaseRecords(BlockPos pos, int dimId, int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT p.id, p.item_id, p.buyer_uuid, p.purchase_count, p.total_price, p.timestamp, d.item_nbt "
         "FROM purchase_records p "
         "JOIN item_definitions d ON p.item_id = d.item_id "
         "WHERE p.dim_id = ? AND p.pos_x = ? AND p.pos_y = ? AND p.pos_z = ? "
         "ORDER BY p.timestamp DESC LIMIT ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        limit
    );

    std::vector<PurchaseRecordData> records;
    for (const auto& row : results) {
        if (row.size() >= 7) {
            PurchaseRecordData data;
            data.id            = std::stoi(row[0]);
            data.dimId         = dimId;
            data.pos           = pos;
            data.itemId        = std::stoi(row[1]);
            data.buyerUuid     = row[2];
            data.purchaseCount = std::stoi(row[3]);
            data.totalPrice    = std::stod(row[4]);
            data.timestamp     = row[5];
            data.itemNbt       = row[6];
            records.push_back(data);
        }
    }
    return records;
}

std::vector<RecycleItemData> ShopRepository::findAllRecycleItems(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT r.item_id, r.price, r.min_durability, r.required_enchants, r.max_recycle_count, "
         "r.current_recycled_count, d.item_nbt FROM recycle_shop_items r "
         "JOIN item_definitions d ON r.item_id = d.item_id "
         "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    std::vector<RecycleItemData> items;
    for (const auto& row : results) {
        if (row.size() >= 7) {
            RecycleItemData data;
            data.dimId                = dimId;
            data.pos                  = pos;
            data.itemId               = std::stoi(row[0]);
            data.price                = std::stod(row[1]);
            data.minDurability        = std::stoi(row[2]);
            data.requiredEnchants     = row[3];
            data.maxRecycleCount      = std::stoi(row[4]);
            data.currentRecycledCount = std::stoi(row[5]);
            data.itemNbt              = row[6];
            items.push_back(data);
        }
    }
    return items;
}

std::optional<RecycleItemData> ShopRepository::findRecycleItem(BlockPos pos, int dimId, int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT r.price, r.min_durability, r.required_enchants, r.max_recycle_count, "
         "r.current_recycled_count, d.item_nbt FROM recycle_shop_items r "
         "JOIN item_definitions d ON r.item_id = d.item_id "
         "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ? AND r.item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );

    if (results.empty() || results[0].size() < 6) {
        return std::nullopt;
    }

    const auto&     row = results[0];
    RecycleItemData data;
    data.dimId                = dimId;
    data.pos                  = pos;
    data.itemId               = itemId;
    data.price                = std::stod(row[0]);
    data.minDurability        = std::stoi(row[1]);
    data.requiredEnchants     = row[2];
    data.maxRecycleCount      = std::stoi(row[3]);
    data.currentRecycledCount = std::stoi(row[4]);
    data.itemNbt              = row[5];
    return data;
}

bool ShopRepository::upsertRecycleItem(const RecycleItemData& item) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, "
        "min_durability, required_enchants, max_recycle_count, current_recycled_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_id) DO UPDATE SET price = "
        "excluded.price, min_durability = excluded.min_durability, required_enchants = "
        "excluded.required_enchants, max_recycle_count = excluded.max_recycle_count;",
        item.dimId,
        item.pos.x,
        item.pos.y,
        item.pos.z,
        item.itemId,
        item.price,
        item.minDurability,
        item.requiredEnchants,
        item.maxRecycleCount
    );
}

bool ShopRepository::updateRecycleItem(BlockPos pos, int dimId, int itemId, double price, int maxCount) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE recycle_shop_items SET price = ?, max_recycle_count = ? "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?;",
        price,
        maxCount,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
}

bool ShopRepository::incrementRecycledCount(BlockPos pos, int dimId, int itemId, int amount) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE recycle_shop_items SET current_recycled_count = current_recycled_count + ? "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?;",
        amount,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
}

bool ShopRepository::addRecycleRecord(const RecycleRecordData& record) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO recycle_records (dim_id, pos_x, pos_y, pos_z, item_id, recycler_uuid, recycle_count, "
        "total_price) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        record.dimId,
        record.pos.x,
        record.pos.y,
        record.pos.z,
        record.itemId,
        record.recyclerUuid,
        record.recycleCount,
        record.totalPrice
    );
}

std::vector<RecycleRecordData> ShopRepository::getRecycleRecords(BlockPos pos, int dimId, int itemId, int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT id, recycler_uuid, recycle_count, total_price, timestamp FROM recycle_records "
         "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ? "
         "ORDER BY timestamp DESC LIMIT ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId,
        limit
    );

    std::vector<RecycleRecordData> records;
    for (const auto& row : results) {
        if (row.size() >= 5) {
            RecycleRecordData data;
            data.id           = std::stoi(row[0]);
            data.dimId        = dimId;
            data.pos          = pos;
            data.itemId       = itemId;
            data.recyclerUuid = row[1];
            data.recycleCount = std::stoi(row[2]);
            data.totalPrice   = std::stod(row[3]);
            data.timestamp    = row[4];
            records.push_back(data);
        }
    }
    return records;
}

} // namespace CT