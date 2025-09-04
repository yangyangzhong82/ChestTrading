#include "Sqlite3Wrapper.h"
#include <iostream>

Sqlite3Wrapper::Sqlite3Wrapper() : db(nullptr) {}

Sqlite3Wrapper::~Sqlite3Wrapper() {
    close();
}

bool Sqlite3Wrapper::open(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
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
