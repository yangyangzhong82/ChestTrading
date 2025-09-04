#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper

namespace CT {

void showChestLockForm(Player& player, BlockPos pos, int dimId) {
    ll::form::SimpleForm fm("上锁箱子", "你确定要上锁这个箱子吗？");

    fm.appendButton("确定", [pos, dimId](Player& p) {
        logger.info(
            "玩家 {} 选择上锁位于维度 {} 的 ({}, {}, {}) 的箱子。",
            p.getUuid().asString(),
            dimId,
            pos.x,
            pos.y,
            pos.z
        );
        // 获取 Sqlite3Wrapper 单例实例
        Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
        // 确保数据库已打开，这里假设数据库路径是固定的，或者在程序启动时已打开
        // 如果数据库未打开，这里需要处理错误或重新打开
        // 为了简化，这里假设 db 已经打开，或者在其他地方处理了打开逻辑
        // db.open("path/to/your/database.db"); // 如果需要，可以在这里打开数据库

        // 创建 locked_chests 表（如果不存在）
        db.execute(
            "CREATE TABLE IF NOT EXISTS locked_chests ("
            "player_uuid TEXT NOT NULL,"
            "dim_id INTEGER NOT NULL,"
            "pos_x INTEGER NOT NULL,"
            "pos_y INTEGER NOT NULL,"
            "pos_z INTEGER NOT NULL,"
            "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z)" // 使用位置作为主键，确保唯一性
            ");"
        );

        // 插入上锁箱子信息
        db.execute(
            "INSERT OR REPLACE INTO locked_chests (player_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?);",
            p.getUuid().asString(),
            static_cast<int>(dimId),
            pos.x,
            pos.y,
            pos.z
        );
        logger.info("箱子信息已存入数据库。");
    });

    fm.appendButton("取消", [](Player& p) { logger.info("玩家 {} 取消了上锁操作。", p.getUuid().asString()); });

    fm.sendTo(player);
}

} // namespace CT
