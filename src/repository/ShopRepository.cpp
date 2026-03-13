#include "ShopRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"
#include <algorithm>

namespace CT {

namespace {

constexpr int UNSET_INT = -2147483647;

struct TradeQueryArgs {
    std::string actorUuid;
    int         dimId;
    int         posX;
    int         posY;
    int         posZ;
    int         itemId;
    int         officialMode;
};

TradeQueryArgs makeTradeQueryArgs(const TradeRecordQuery& query) {
    TradeQueryArgs args;
    args.actorUuid    = query.actorUuid.value_or("");
    args.dimId        = query.dimId.value_or(-1);
    args.posX         = query.pos ? query.pos->x : UNSET_INT;
    args.posY         = query.pos ? query.pos->y : UNSET_INT;
    args.posZ         = query.pos ? query.pos->z : UNSET_INT;
    args.itemId       = query.itemId.value_or(-1);
    args.officialMode = query.officialOnly.has_value() ? (*query.officialOnly ? 1 : 0) : -1;
    return args;
}

bool tradeRecordTimeDesc(const TradeRecordData& a, const TradeRecordData& b) {
    if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
    if (a.id != b.id) return a.id > b.id;
    return static_cast<int>(a.kind) < static_cast<int>(b.kind);
}

} // namespace

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

    return parseSingleRow<ShopItemData>(results, 4, [&](DbRowParser r) {
        return ShopItemData{dimId, pos, itemId, r.getString(3), r.getDouble(1), r.getInt(2), r.getInt(0)};
    });
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

    return parseRows<ShopItemData>(results, 5, [&](DbRowParser r) {
        return ShopItemData{dimId, pos, r.getInt(0), r.getString(4), r.getDouble(2), r.getInt(3), r.getInt(1)};
    });
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

    return parseRows<PurchaseRecordData>(results, 7, [&](DbRowParser r) {
        return PurchaseRecordData{
            r.getInt(0),
            dimId,
            pos,
            r.getInt(1),
            r.getString(2),
            r.getInt(3),
            r.getDouble(4),
            r.getString(5),
            r.getString(6)
        };
    });
}

std::vector<PurchaseRecordData> ShopRepository::getPlayerPurchaseHistory(const std::string& playerUuid, int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT p.id, p.dim_id, p.pos_x, p.pos_y, p.pos_z, p.item_id, p.buyer_uuid, "
         "p.purchase_count, p.total_price, p.timestamp, d.item_nbt "
         "FROM purchase_records p "
         "JOIN item_definitions d ON p.item_id = d.item_id "
         "WHERE p.buyer_uuid = ? "
         "GROUP BY p.dim_id, p.pos_x, p.pos_y, p.pos_z, p.item_id "
         "ORDER BY MAX(p.timestamp) DESC LIMIT ?;",
        playerUuid,
        limit
    );

    return parseRows<PurchaseRecordData>(results, 11, [](DbRowParser r) {
        return PurchaseRecordData{
            r.getInt(0),
            r.getInt(1),
            BlockPos{r.getInt(2), r.getInt(3), r.getInt(4)},
            r.getInt(5),
            r.getString(6),
            r.getInt(7),
            r.getDouble(8),
            r.getString(9),
            r.getString(10)
        };
    });
}

std::optional<PurchaseRecordData> ShopRepository::getLatestPurchaseRecord(const std::string& playerUuid) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT p.id, p.dim_id, p.pos_x, p.pos_y, p.pos_z, p.item_id, p.buyer_uuid, "
        "p.purchase_count, p.total_price, p.timestamp, d.item_nbt "
        "FROM purchase_records p "
        "JOIN item_definitions d ON p.item_id = d.item_id "
        "WHERE p.buyer_uuid = ? "
        "ORDER BY p.timestamp DESC, p.id DESC LIMIT 1;",
        playerUuid
    );

    return parseSingleRow<PurchaseRecordData>(results, 11, [](DbRowParser r) {
        return PurchaseRecordData{
            r.getInt(0),
            r.getInt(1),
            BlockPos{r.getInt(2), r.getInt(3), r.getInt(4)},
            r.getInt(5),
            r.getString(6),
            r.getInt(7),
            r.getDouble(8),
            r.getString(9),
            r.getString(10)
        };
    });
}

std::vector<TradeRecordData> ShopRepository::getTradeRecords(const TradeRecordQuery& query) {
    auto&                        db   = Sqlite3Wrapper::getInstance();
    auto                         args = makeTradeQueryArgs(query);
    std::vector<TradeRecordData> records;
    const std::string            unsetPos = std::to_string(UNSET_INT);

    if (query.includePurchase) {
        std::string purchaseSql =
            "SELECT p.id, p.dim_id, p.pos_x, p.pos_y, p.pos_z, p.item_id, p.buyer_uuid, "
            "p.purchase_count, p.total_price, p.timestamp, d.item_nbt, "
            "COALESCE(c.player_uuid, ''), COALESCE(c.shop_name, ''), COALESCE(c.type, 2) "
            "FROM purchase_records p "
            "JOIN item_definitions d ON p.item_id = d.item_id "
            "LEFT JOIN chests c ON c.dim_id = p.dim_id AND c.pos_x = p.pos_x AND c.pos_y = p.pos_y AND c.pos_z = p.pos_z "
            "WHERE (? = '' OR p.buyer_uuid = ?) "
            "AND (? < 0 OR p.dim_id = ?) "
            "AND (? = " + unsetPos + " OR p.pos_x = ?) "
            "AND (? = " + unsetPos + " OR p.pos_y = ?) "
            "AND (? = " + unsetPos + " OR p.pos_z = ?) "
            "AND (? < 0 OR p.item_id = ?) "
            "AND (? = -1 OR (? = 1 AND c.type = 5) OR (? = 0 AND c.type = 2)) "
            "ORDER BY p.timestamp DESC, p.id DESC;";

        auto purchaseRows = db.query(
            purchaseSql,
            args.actorUuid,
            args.actorUuid,
            args.dimId,
            args.dimId,
            args.posX,
            args.posX,
            args.posY,
            args.posY,
            args.posZ,
            args.posZ,
            args.itemId,
            args.itemId,
            args.officialMode,
            args.officialMode,
            args.officialMode
        );

        auto purchaseRecords = parseRows<TradeRecordData>(purchaseRows, 14, [](DbRowParser r) {
            int chestType = r.getIntOr(13, 2);
            return TradeRecordData{
                r.getInt(0),
                TradeRecordKind::Purchase,
                r.getInt(1),
                BlockPos{r.getInt(2), r.getInt(3), r.getInt(4)},
                r.getInt(5),
                r.getString(6),
                r.getString(11),
                r.getString(12),
                r.getString(10),
                r.getInt(7),
                r.getDouble(8),
                r.getString(9),
                chestType == 5
            };
        });
        records.insert(records.end(), purchaseRecords.begin(), purchaseRecords.end());
    }

    if (query.includeRecycle) {
        std::string recycleSql =
            "SELECT r.id, r.dim_id, r.pos_x, r.pos_y, r.pos_z, r.item_id, r.recycler_uuid, "
            "r.recycle_count, r.total_price, r.timestamp, d.item_nbt, "
            "COALESCE(c.player_uuid, ''), COALESCE(c.shop_name, ''), COALESCE(c.type, 3) "
            "FROM recycle_records r "
            "JOIN item_definitions d ON r.item_id = d.item_id "
            "LEFT JOIN chests c ON c.dim_id = r.dim_id AND c.pos_x = r.pos_x AND c.pos_y = r.pos_y AND c.pos_z = r.pos_z "
            "WHERE (? = '' OR r.recycler_uuid = ?) "
            "AND (? < 0 OR r.dim_id = ?) "
            "AND (? = " + unsetPos + " OR r.pos_x = ?) "
            "AND (? = " + unsetPos + " OR r.pos_y = ?) "
            "AND (? = " + unsetPos + " OR r.pos_z = ?) "
            "AND (? < 0 OR r.item_id = ?) "
            "AND (? = -1 OR (? = 1 AND c.type = 6) OR (? = 0 AND c.type = 3)) "
            "ORDER BY r.timestamp DESC, r.id DESC;";

        auto recycleRows = db.query(
            recycleSql,
            args.actorUuid,
            args.actorUuid,
            args.dimId,
            args.dimId,
            args.posX,
            args.posX,
            args.posY,
            args.posY,
            args.posZ,
            args.posZ,
            args.itemId,
            args.itemId,
            args.officialMode,
            args.officialMode,
            args.officialMode
        );

        auto recycleRecords = parseRows<TradeRecordData>(recycleRows, 14, [](DbRowParser r) {
            int chestType = r.getIntOr(13, 3);
            return TradeRecordData{
                r.getInt(0),
                TradeRecordKind::Recycle,
                r.getInt(1),
                BlockPos{r.getInt(2), r.getInt(3), r.getInt(4)},
                r.getInt(5),
                r.getString(6),
                r.getString(11),
                r.getString(12),
                r.getString(10),
                r.getInt(7),
                r.getDouble(8),
                r.getString(9),
                chestType == 6
            };
        });
        records.insert(records.end(), recycleRecords.begin(), recycleRecords.end());
    }

    std::sort(records.begin(), records.end(), tradeRecordTimeDesc);
    return records;
}

std::vector<RecycleItemData> ShopRepository::findAllRecycleItems(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT r.item_id, r.price, r.min_durability, r.required_enchants, r.max_recycle_count, "
         "r.current_recycled_count, d.item_nbt, r.required_aux_value FROM recycle_shop_items r "
         "JOIN item_definitions d ON r.item_id = d.item_id "
         "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    return parseRows<RecycleItemData>(results, 7, [&](DbRowParser r) {
        return RecycleItemData{
            dimId,
            pos,
            r.getInt(0),
            r.getString(6),
            r.getDouble(1),
            r.getInt(2),
            r.getString(3),
            r.getInt(4),
            r.getInt(5),
            r.getIntOr(7, -1)
        };
    });
}

std::optional<RecycleItemData> ShopRepository::findRecycleItem(BlockPos pos, int dimId, int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT r.price, r.min_durability, r.required_enchants, r.max_recycle_count, "
         "r.current_recycled_count, d.item_nbt, r.required_aux_value FROM recycle_shop_items r "
         "JOIN item_definitions d ON r.item_id = d.item_id "
         "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ? AND r.item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );

    return parseSingleRow<RecycleItemData>(results, 6, [&](DbRowParser r) {
        return RecycleItemData{
            dimId,
            pos,
            itemId,
            r.getString(5),
            r.getDouble(0),
            r.getInt(1),
            r.getString(2),
            r.getInt(3),
            r.getInt(4),
            r.getIntOr(6, -1)
        };
    });
}

bool ShopRepository::upsertRecycleItem(const RecycleItemData& item) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, "
        "min_durability, required_enchants, max_recycle_count, current_recycled_count, required_aux_value) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_id) DO UPDATE SET price = "
        "excluded.price, min_durability = excluded.min_durability, required_enchants = "
        "excluded.required_enchants, max_recycle_count = excluded.max_recycle_count, "
        "required_aux_value = excluded.required_aux_value;",
        item.dimId,
        item.pos.x,
        item.pos.y,
        item.pos.z,
        item.itemId,
        item.price,
        item.minDurability,
        item.requiredEnchants,
        item.maxRecycleCount,
        item.requiredAuxValue
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

    return parseRows<RecycleRecordData>(results, 5, [&](DbRowParser r) {
        return RecycleRecordData{
            r.getInt(0),
            dimId,
            pos,
            itemId,
            r.getString(1),
            r.getInt(2),
            r.getDouble(3),
            r.getString(4)
        };
    });
}

std::vector<PublicShopItemData> ShopRepository::findAllPublicShopItems() {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.shop_name, c.type, "
         "s.item_id, s.price, s.db_count, d.item_nbt "
         "FROM chests c "
         "JOIN shop_items s ON c.dim_id = s.dim_id AND c.pos_x = s.pos_x AND c.pos_y = s.pos_y AND c.pos_z = s.pos_z "
         "JOIN item_definitions d ON s.item_id = d.item_id "
         "WHERE c.is_public = 1 AND c.type IN (2, 5) AND s.db_count > 0 "
         "ORDER BY c.shop_name, c.player_uuid;"
    );

    return parseRows<PublicShopItemData>(results, 11, [](DbRowParser r) {
        int chestType = r.getInt(6);
        return PublicShopItemData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            r.getString(5),
            r.getInt(7),
            r.getString(10),
            r.getDouble(8),
            r.getInt(9),
            chestType == 5  // AdminShop = 5
        };
    });
}

std::vector<PublicRecycleItemData> ShopRepository::findAllPublicRecycleItems() {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.shop_name, c.type, "
                             "r.item_id, r.price, d.item_nbt "
                             "FROM chests c "
                             "JOIN recycle_shop_items r ON c.dim_id = r.dim_id AND c.pos_x = r.pos_x AND c.pos_y = "
                             "r.pos_y AND c.pos_z = r.pos_z "
                             "JOIN item_definitions d ON r.item_id = d.item_id "
                             "WHERE c.is_public = 1 AND c.type IN (3, 6) "
                             "ORDER BY c.shop_name, c.player_uuid;");

    return parseRows<PublicRecycleItemData>(results, 10, [](DbRowParser r) {
        int chestType = r.getInt(6);
        return PublicRecycleItemData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            r.getString(5),
            r.getInt(7),
            r.getString(9),
            r.getDouble(8),
            chestType == 6  // AdminRecycle = 6
        };
    });
}

std::vector<ChestSalesData> ShopRepository::getChestSalesRanking(int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.shop_name, "
         "COALESCE(SUM(p.purchase_count), 0) as total_count, "
         "COALESCE(SUM(p.total_price), 0) as total_revenue, "
         "MAX(p.timestamp) as last_sale "
         "FROM chests c "
         "LEFT JOIN purchase_records p ON c.dim_id = p.dim_id AND c.pos_x = p.pos_x "
         "AND c.pos_y = p.pos_y AND c.pos_z = p.pos_z "
         "WHERE c.type IN (2, 5) "
         "GROUP BY c.dim_id, c.pos_x, c.pos_y, c.pos_z "
         "ORDER BY total_revenue DESC, total_count DESC "
         "LIMIT ?;",
        limit
    );

    return parseRows<ChestSalesData>(results, 9, [](DbRowParser r) {
        return ChestSalesData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            r.getString(5),
            r.getInt(6),
            r.getDouble(7),
            r.getString(8)
        };
    });
}

std::vector<ChestSalesData> ShopRepository::getRecycleChestSalesRanking(int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.shop_name, "
        "COALESCE(SUM(r.recycle_count), 0) as total_count, "
        "COALESCE(SUM(r.total_price), 0) as total_revenue, "
        "MAX(r.timestamp) as last_sale "
        "FROM chests c "
        "LEFT JOIN recycle_records r ON c.dim_id = r.dim_id AND c.pos_x = r.pos_x "
        "AND c.pos_y = r.pos_y AND c.pos_z = r.pos_z "
        "WHERE c.type IN (3, 6) "
        "GROUP BY c.dim_id, c.pos_x, c.pos_y, c.pos_z "
        "ORDER BY total_revenue DESC, total_count DESC "
        "LIMIT ?;",
        limit
    );

    return parseRows<ChestSalesData>(results, 9, [](DbRowParser r) {
        return ChestSalesData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            r.getString(5),
            r.getInt(6),
            r.getDouble(7),
            r.getString(8)
        };
    });
}

std::vector<PlayerSalesData> ShopRepository::getPlayerSalesRanking(int limit) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "WITH player_stats AS ("
        "  SELECT c.player_uuid AS owner_uuid, "
        "         SUM(p.purchase_count) AS total_count, "
        "         SUM(p.total_price) AS total_revenue, "
        "         MAX(p.timestamp) AS last_sale_time "
        "  FROM chests c "
        "  JOIN purchase_records p "
        "    ON c.dim_id = p.dim_id AND c.pos_x = p.pos_x AND c.pos_y = p.pos_y AND c.pos_z = p.pos_z "
        "  WHERE c.type IN (2, 5) "
        "  GROUP BY c.player_uuid "
        "), "
        "last_trade AS ("
        "  SELECT c.player_uuid AS owner_uuid, p.dim_id, p.pos_x, p.pos_y, p.pos_z, "
        "         p.purchase_count, p.total_price, p.timestamp, "
        "         ROW_NUMBER() OVER (PARTITION BY c.player_uuid ORDER BY p.timestamp DESC, p.id DESC) AS rn "
        "  FROM purchase_records p "
        "  JOIN chests c "
        "    ON c.dim_id = p.dim_id AND c.pos_x = p.pos_x AND c.pos_y = p.pos_y AND c.pos_z = p.pos_z "
        "  WHERE c.type IN (2, 5) "
        ") "
        "SELECT ps.owner_uuid, ps.total_count, ps.total_revenue, ps.last_sale_time, "
        "       COALESCE(lt.dim_id, 0), COALESCE(lt.pos_x, 0), COALESCE(lt.pos_y, 0), COALESCE(lt.pos_z, 0), "
        "       COALESCE(lt.purchase_count, 0), COALESCE(lt.total_price, 0) "
        "FROM player_stats ps "
        "LEFT JOIN last_trade lt ON ps.owner_uuid = lt.owner_uuid AND lt.rn = 1 "
        "ORDER BY ps.total_revenue DESC, ps.total_count DESC "
        "LIMIT ?;",
        limit
    );

    return parseRows<PlayerSalesData>(results, 10, [](DbRowParser r) {
        return PlayerSalesData{
            r.getString(0),
            r.getInt(1),
            r.getDouble(2),
            r.getString(3),
            r.getInt(4),
            BlockPos{r.getInt(5), r.getInt(6), r.getInt(7)},
            r.getInt(8),
            r.getDouble(9)
        };
    });
}

} // namespace CT
