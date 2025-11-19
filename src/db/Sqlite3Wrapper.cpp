#include "Sqlite3Wrapper.h"
#include "logger.h"
#include <iostream>
#include <vector>



Sqlite3Wrapper::Sqlite3Wrapper() : db(nullptr) {}

Sqlite3Wrapper::~Sqlite3Wrapper() {
    close();
}

bool Sqlite3Wrapper::open(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // 检查旧表 locked_chests 是否存在
    std::vector<std::vector<std::string>> tables =
        query("SELECT name FROM sqlite_master WHERE type='table' AND name='locked_chests';");
    if (!tables.empty()) {
        // 如果存在，则重命名并添加新列
        CT::logger.info("检测到旧数据表 `locked_chests`，正在迁移...");
        if (execute_unsafe("ALTER TABLE locked_chests RENAME TO chests;") &&
            execute_unsafe("ALTER TABLE chests ADD COLUMN type INTEGER DEFAULT 0;")) {
            CT::logger.info("数据表迁移成功！");
        } else {
            CT::logger.error("数据表迁移失败！");
            return false;
        }
    } else {
        // 如果不存在，则创建新表
        const char* create_chests_table = "CREATE TABLE IF NOT EXISTS chests ("
                                          "player_uuid TEXT NOT NULL,"
                                          "dim_id INTEGER NOT NULL,"
                                          "pos_x INTEGER NOT NULL,"
                                          "pos_y INTEGER NOT NULL,"
                                          "pos_z INTEGER NOT NULL,"
                                          "type INTEGER DEFAULT 0,"
                                          "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z));";
        if (!execute_unsafe(create_chests_table)) {
            return false;
        }
    }

    // 总是确保 shared_chests 表存在
    const char* create_shared_chests_table = "CREATE TABLE IF NOT EXISTS shared_chests ("
                                             "player_uuid TEXT NOT NULL,"
                                             "dim_id INTEGER NOT NULL,"
                                             "pos_x INTEGER NOT NULL,"
                                             "pos_y INTEGER NOT NULL,"
                                             "pos_z INTEGER NOT NULL,"
                                             "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                             "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_shared_chests_table)) {
        return false;
    }

    // 总是确保 shop_items 表存在
    const char* create_shop_items_table = "CREATE TABLE IF NOT EXISTS shop_items ("
                                          "dim_id INTEGER NOT NULL,"
                                          "pos_x INTEGER NOT NULL,"
                                          "pos_y INTEGER NOT NULL,"
                                          "pos_z INTEGER NOT NULL,"
                                          "slot INTEGER NOT NULL,"
                                          "item_nbt TEXT NOT NULL,"
                                          "price INTEGER NOT NULL,"
                                          "db_count INTEGER NOT NULL DEFAULT 0," // 添加 db_count 字段
                                          "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_nbt),"
                                          "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                          "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_shop_items_table)) {
        return false;
    }

    // 检查 shop_items 表是否缺少 db_count 字段，如果缺少则添加
    std::vector<std::vector<std::string>> tables_info =
        query("PRAGMA table_info(shop_items);");
    bool has_db_count_column = false;
    for (const auto& row : tables_info) {
        if (row[1] == "db_count") { // row[1] 是列名
            has_db_count_column = true;
            break;
        }
    }

    if (!has_db_count_column) {
        CT::logger.info("检测到 `shop_items` 表缺少 `db_count` 字段，正在添加...");
        if (execute_unsafe("ALTER TABLE shop_items ADD COLUMN db_count INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.info("`db_count` 字段添加成功！");
        } else {
            CT::logger.error("`db_count` 字段添加失败！");
            return false;
        }
    }

    // 总是确保 recycle_shop_items 表存在
    const char* create_recycle_shop_items_table = "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
                                                  "dim_id INTEGER NOT NULL,"
                                                  "pos_x INTEGER NOT NULL,"
                                                  "pos_y INTEGER NOT NULL,"
                                                  "pos_z INTEGER NOT NULL,"
                                                  "item_nbt TEXT NOT NULL,"
                                                  "price INTEGER NOT NULL,"
                                                  "min_durability INTEGER NOT NULL DEFAULT 0,"
                                                  "required_enchant_id INTEGER NOT NULL DEFAULT -1,"
                                                  "required_enchant_level INTEGER NOT NULL DEFAULT 0,"
                                                  "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_nbt),"
                                                  "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                                  "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_recycle_shop_items_table)) {
        return false;
    }
    // 创建回收记录表
    const char* create_recycle_records_table = "CREATE TABLE IF NOT EXISTS recycle_records ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "dim_id INTEGER NOT NULL,"
                                               "pos_x INTEGER NOT NULL,"
                                               "pos_y INTEGER NOT NULL,"
                                               "pos_z INTEGER NOT NULL,"
                                               "item_nbt TEXT NOT NULL,"
                                               "recycler_uuid TEXT NOT NULL,"
                                               "recycle_count INTEGER NOT NULL,"
                                               "total_price INTEGER NOT NULL,"
                                               "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                               "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_nbt) REFERENCES "
                                               "recycle_shop_items(dim_id, pos_x, pos_y, pos_z, item_nbt) ON DELETE "
                                               "CASCADE);";
    if (!execute_unsafe(create_recycle_records_table)) {
        return false;
    }
    // 创建购买记录表
    const char* create_purchase_records_table = "CREATE TABLE IF NOT EXISTS purchase_records ("
                                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                "dim_id INTEGER NOT NULL,"
                                                "pos_x INTEGER NOT NULL,"
                                                "pos_y INTEGER NOT NULL,"
                                                "pos_z INTEGER NOT NULL,"
                                                "item_nbt TEXT NOT NULL,"
                                                "buyer_uuid TEXT NOT NULL,"
                                                "purchase_count INTEGER NOT NULL,"
                                                "total_price INTEGER NOT NULL,"
                                                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_nbt) REFERENCES "
                                                "shop_items(dim_id, pos_x, pos_y, pos_z, item_nbt) ON DELETE "
                                                "CASCADE);";
    if (!execute_unsafe(create_purchase_records_table)) {
        return false;
    }


    // 检查 recycle_shop_items 表是否缺少新字段，如果缺少则添加
    std::vector<std::vector<std::string>> recycle_tables_info = query("PRAGMA table_info(recycle_shop_items);");
    bool has_min_durability_column = false;
    bool has_req_enchant_id_column = false;
    bool has_req_enchant_lvl_column = false;
    bool has_max_recycle_count_column = false;
    bool has_current_recycled_count_column = false;

    for (const auto& row : recycle_tables_info) {
        if (row[1] == "min_durability") has_min_durability_column = true;
        if (row[1] == "required_enchant_id") has_req_enchant_id_column = true;
        if (row[1] == "required_enchant_level") has_req_enchant_lvl_column = true;
        if (row[1] == "max_recycle_count") has_max_recycle_count_column = true;
        if (row[1] == "current_recycled_count") has_current_recycled_count_column = true;
    }

    if (!has_min_durability_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `min_durability` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN min_durability INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`min_durability` 字段添加失败！");
            return false;
        }
    }
    if (!has_req_enchant_id_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `required_enchant_id` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchant_id INTEGER NOT NULL DEFAULT -1;")) {
            CT::logger.error("`required_enchant_id` 字段添加失败！");
            return false;
        }
    }
    if (!has_req_enchant_lvl_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `required_enchant_level` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchant_level INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`required_enchant_level` 字段添加失败！");
            return false;
        }
    }
    if (!has_max_recycle_count_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `max_recycle_count` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN max_recycle_count INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`max_recycle_count` 字段添加失败！");
            return false;
        }
    }
    if (!has_current_recycled_count_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `current_recycled_count` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN current_recycled_count INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`current_recycled_count` 字段添加失败！");
            return false;
        }
    }

    return true;
}

void Sqlite3Wrapper::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Sqlite3Wrapper::execute_unsafe(const std::string& sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

static int callback(void* data, int argc, char** argv, char** azColName) {
    auto* results = static_cast<std::vector<std::vector<std::string>>*>(data);
    std::vector<std::string> row;
    for (int i = 0; i < argc; i++) {
        row.push_back(argv[i] ? argv[i] : "NULL");
    }
    results->push_back(row);
    return 0;
}

std::vector<std::vector<std::string>> Sqlite3Wrapper::query_unsafe(const std::string& sql) {
    std::vector<std::vector<std::string>> results;
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), callback, &results, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return results;
}
