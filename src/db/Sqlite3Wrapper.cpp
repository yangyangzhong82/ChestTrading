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
    return execute_unsafe(create_shared_chests_table);
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
