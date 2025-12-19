#include "SchemaMigration.h"
#include "Sqlite3Wrapper.h"
#include "logger.h"

bool SchemaMigration::run(Sqlite3Wrapper& db) {
    int currentVersion = getSchemaVersion(db);
    CT::logger.info("当前数据库 schema 版本: {}", currentVersion);

    using MigrationFunc                   = std::function<bool(Sqlite3Wrapper&)>;
    std::vector<MigrationFunc> migrations = {
        migrateToV1,
        migrateToV2,
    };

    for (int v = currentVersion; v < static_cast<int>(migrations.size()); ++v) {
        CT::logger.info("执行迁移: V{} -> V{}", v, v + 1);
        Transaction txn(db);
        if (!txn.isActive()) {
            CT::logger.error("无法开始迁移事务 V{}", v + 1);
            return false;
        }

        if (!migrations[v](db)) {
            CT::logger.error("迁移到 V{} 失败！", v + 1);
            return false;
        }

        setSchemaVersion(db, v + 1);
        if (!txn.commit()) {
            CT::logger.error("无法提交迁移事务 V{}", v + 1);
            return false;
        }
        CT::logger.info("迁移到 V{} 成功", v + 1);
    }

    CT::logger.info("数据库表结构初始化完成，当前版本: {}", getSchemaVersion(db));
    return true;
}

int SchemaMigration::getSchemaVersion(Sqlite3Wrapper& db) {
    auto result = db.query_unsafe("PRAGMA user_version;");
    if (!result.empty() && !result[0].empty()) {
        return std::stoi(result[0][0]);
    }
    return 0;
}

void SchemaMigration::setSchemaVersion(Sqlite3Wrapper& db, int version) {
    db.execute_unsafe("PRAGMA user_version = " + std::to_string(version) + ";");
}

bool SchemaMigration::migrateToV1(Sqlite3Wrapper& db) {
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS chests ("
        "player_uuid TEXT NOT NULL, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, type INTEGER NOT NULL DEFAULT 0, "
        "shop_name TEXT NOT NULL DEFAULT '', enable_floating_text INTEGER NOT NULL DEFAULT 1, "
        "enable_fake_item INTEGER NOT NULL DEFAULT 1, is_public INTEGER NOT NULL DEFAULT 1, "
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z));",

        "CREATE TABLE IF NOT EXISTS shared_chests ("
        "player_uuid TEXT NOT NULL, owner_uuid TEXT NOT NULL, dim_id INTEGER NOT NULL, "
        "pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "PRIMARY KEY (player_uuid, dim_id, pos_x, pos_y, pos_z), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS item_definitions (item_id INTEGER PRIMARY KEY AUTOINCREMENT, item_nbt TEXT NOT "
        "NULL UNIQUE);",
        "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);",

        "CREATE TABLE IF NOT EXISTS shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "slot INTEGER, item_id INTEGER NOT NULL, price REAL NOT NULL, db_count INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE, "
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS purchase_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, item_id INTEGER NOT NULL, buyer_uuid TEXT NOT NULL, "
        "purchase_count INTEGER NOT NULL, total_price REAL NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "item_id INTEGER NOT NULL, price REAL NOT NULL, min_durability INTEGER DEFAULT 0, "
        "required_enchants TEXT NOT NULL DEFAULT '', max_recycle_count INTEGER NOT NULL DEFAULT 0, "
        "current_recycled_count INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE, "
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, item_id INTEGER NOT NULL, recycler_uuid TEXT NOT NULL, "
        "recycle_count INTEGER NOT NULL, total_price REAL NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES recycle_shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);",

        "CREATE INDEX IF NOT EXISTS idx_chests_position ON chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shared_chests_position ON shared_chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shop_items_position ON shop_items(dim_id, pos_x, pos_y, pos_z);"
    };

    for (const char* sql : sqls) {
        if (!db.execute_unsafe(sql)) return false;
    }
    return true;
}

bool SchemaMigration::migrateToV2(Sqlite3Wrapper& db) {
    const char* sqls[] = {
        "CREATE INDEX IF NOT EXISTS idx_chests_player_uuid ON chests(player_uuid);",
        "CREATE INDEX IF NOT EXISTS idx_chests_player_type ON chests(player_uuid, type);",
        "CREATE INDEX IF NOT EXISTS idx_shared_chests_player ON shared_chests(player_uuid);",
        "CREATE INDEX IF NOT EXISTS idx_shop_items_item_id ON shop_items(item_id);",
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_position ON purchase_records(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_timestamp ON purchase_records(timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_buyer ON purchase_records(buyer_uuid);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_shop_items_position ON recycle_shop_items(dim_id, pos_x, pos_y, "
        "pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_shop_items_item_id ON recycle_shop_items(item_id);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_position ON recycle_records(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_item ON recycle_records(dim_id, pos_x, pos_y, pos_z, item_id);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_timestamp ON recycle_records(timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_recycler ON recycle_records(recycler_uuid);"
    };

    for (const char* sql : sqls) {
        if (!db.execute_unsafe(sql)) return false;
    }
    return true;
}
