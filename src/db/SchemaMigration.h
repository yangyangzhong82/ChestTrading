#pragma once

#include <functional>
#include <vector>

class Sqlite3Wrapper;

class SchemaMigration {
public:
    static bool run(Sqlite3Wrapper& db);

private:
    static int  getSchemaVersion(Sqlite3Wrapper& db);
    static void setSchemaVersion(Sqlite3Wrapper& db, int version);

    static bool migrateToV1(Sqlite3Wrapper& db);
    static bool migrateToV2(Sqlite3Wrapper& db);
    static bool migrateToV3(Sqlite3Wrapper& db);
    static bool migrateToV4(Sqlite3Wrapper& db);
    static bool migrateToV5(Sqlite3Wrapper& db);
    static bool migrateToV6(Sqlite3Wrapper& db);
    static bool migrateToV7(Sqlite3Wrapper& db);
    static bool migrateToV8(Sqlite3Wrapper& db);
    static bool migrateToV9(Sqlite3Wrapper& db);
    static bool migrateToV10(Sqlite3Wrapper& db);
    static bool migrateToV11(Sqlite3Wrapper& db);
};
