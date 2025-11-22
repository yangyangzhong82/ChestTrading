# 数据库异步操作使用指南

## 概述

ChestTrading 数据库现在支持异步操作，使用线程池来提高性能。所有数据库操作都可以异步执行，避免阻塞主线程。

## 线程池配置

### 设置线程池大小

在打开数据库之前，可以设置线程池的大小：

```cpp
Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();

// 设置线程池大小为 8 个线程（默认为 4）
db.setThreadPoolSize(8);

// 然后打开数据库
db.open("path/to/database.db");
```

## 异步操作 API

### 1. 异步执行 SQL 语句

```cpp
// 异步执行 INSERT/UPDATE/DELETE 等操作
auto future = db.executeAsync(
    "INSERT INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, ?, ?, ?)",
    playerUuid, dimId, posX, posY, posZ, type
);

// 可以继续执行其他操作...

// 需要结果时，等待完成
bool success = future.get();
if (success) {
    // 操作成功
}
```

### 2. 异步查询

```cpp
// 异步查询数据
auto future = db.queryAsync(
    "SELECT * FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?",
    dimId, posX, posY, posZ
);

// 可以继续执行其他操作...

// 需要结果时，等待完成
auto results = future.get();
for (const auto& row : results) {
    // 处理每一行数据
}
```

### 3. 异步批量操作

```cpp
std::vector<std::string> sqlStatements = {
    "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
    "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
};

std::vector<std::vector<Sqlite3Wrapper::Value>> paramsList = {
    {dimId, posX, posY, posZ, 0, itemId1, price1, count1},
    {dimId, posX, posY, posZ, 1, itemId2, price2, count2}
};

auto future = db.executeBatchAsync(sqlStatements, paramsList);

// 等待批量操作完成
bool success = future.get();
```

## 监控和管理

### 获取待处理任务数

```cpp
size_t pendingTasks = db.getPendingAsyncTasks();
CT::logger.info("待处理的异步任务数: {}", pendingTasks);
```

### 等待所有异步任务完成

```cpp
// 在关闭数据库或模组卸载前，等待所有异步任务完成
db.waitForAllAsyncTasks();
CT::logger.info("所有异步任务已完成");
```

## 使用建议

### 1. 何时使用异步操作

- **适合使用异步**：
  - 非关键路径的数据库操作
  - 批量数据插入/更新
  - 日志记录
  - 统计数据更新
  - 不需要立即返回结果的操作

- **不适合使用异步**：
  - 需要立即获取结果的查询
  - 事务性操作（需要保证顺序）
  - 关键路径上的操作

### 2. 错误处理

```cpp
try {
    auto future = db.executeAsync("INSERT INTO ...", params...);
    bool success = future.get();
    if (!success) {
        CT::logger.error("数据库操作失败");
    }
} catch (const std::exception& e) {
    CT::logger.error("异步操作异常: {}", e.what());
}
```

### 3. 性能优化

```cpp
// 对于大量独立的操作，使用异步可以显著提高性能
std::vector<std::future<bool>> futures;

for (const auto& item : items) {
    futures.push_back(db.executeAsync(
        "INSERT INTO items VALUES (?, ?)",
        item.id, item.name
    ));
}

// 等待所有操作完成
for (auto& future : futures) {
    future.get();
}
```

## 示例：异步保存商店数据

```cpp
void saveShopDataAsync(const ShopData& data) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    
    // 异步保存，不阻塞主线程
    auto future = db.executeAsync(
        "INSERT OR REPLACE INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        data.dimId, data.posX, data.posY, data.posZ,
        data.slot, data.itemId, data.price, data.count
    );
    
    // 可以选择立即返回，或者等待完成
    // future.get(); // 如果需要确认操作成功
}
```

## 注意事项

1. **线程安全**：所有数据库操作都是线程安全的，内部使用互斥锁保护
2. **资源管理**：线程池会在数据库关闭时自动清理
3. **异常处理**：异步操作中的异常会在调用 `future.get()` 时抛出
4. **性能考虑**：线程池大小应根据系统资源和负载调整
5. **WAL 模式**：建议启用 WAL 模式以提高并发性能

## 迁移指南

### 从同步到异步

**之前（同步）：**
```cpp
bool success = db.execute("INSERT INTO ...", params...);
```

**现在（异步）：**
```cpp
auto future = db.executeAsync("INSERT INTO ...", params...);
// ... 其他操作 ...
bool success = future.get(); // 需要时获取结果
```

### 保持兼容性

原有的同步 API 仍然可用，不需要强制迁移：

```cpp
// 同步操作仍然有效
bool success = db.execute("INSERT INTO ...", params...);
auto results = db.query("SELECT * FROM ...", params...);
