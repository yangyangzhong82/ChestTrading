# 数据库异步化优化文档

## 概述

本文档说明了对 ChestTrading 插件中数据库查询操作的异步化优化。这些优化旨在减少主线程阻塞，提高服务器在大规模场景下的性能。

## 优化背景

在服务器规模较大时，同步的数据库查询和 SNBT 解析会在主线程运行，导致服务器卡顿。特别是以下场景：

- 查看购买记录（可能有大量历史记录）
- 查看回收委托列表（需要解析多个物品的 NBT）
- 查看回收记录详情（需要查询历史记录并解析玩家信息）

## 已异步化的函数

### 1. ShopForm.cpp

#### `showPurchaseRecordsForm`
- **功能**: 显示商店的购买记录
- **优化**: 异步查询 `purchase_records` 表
- **优先级**: 高（纯展示性查询，数据量可能很大）

**实现要点**:
```cpp
// 1. 在主线程获取玩家UUID
std::string playerUuid = player.getUuid().asString();

// 2. 异步查询数据库
auto future = db.queryAsync(...);

// 3. 在后台线程等待查询完成
std::thread([future = std::move(future), playerUuid, pos, dimId]() mutable {
    auto records = future.get();
    
    // 4. 回调到主线程显示表单
    ll::thread::ServerThreadExecutor::getDefault().execute([...]() {
        auto* player = ll::service::getLevel()->getPlayer(...);
        // 显示表单
    });
}).detach();
```

### 2. RecycleForm.cpp

#### `showViewRecycleCommissionsForm`
- **功能**: 显示回收商店的所有回收委托
- **优化**: 异步查询 `recycle_shop_items` 表
- **优先级**: 高（需要解析多个物品的 NBT，计算量大）

#### `showCommissionDetailsForm`
- **功能**: 显示特定回收委托的详细记录
- **优化**: 异步查询 `recycle_records` 表
- **优先级**: 高（纯展示性查询，数据量可能很大）

## 未异步化的函数（及原因）

### 保持同步的查询

以下查询保持同步执行，原因如下：

1. **`getChestDetails`**: 
   - 简单查询 + 有 ChestCache
   - 频率极高
   - 查询结果直接影响后续逻辑

2. **购买/回收时的库存检查**:
   - 涉及经济结算
   - 流程必须串行
   - 放在主线程保证安全性和一致性

3. **商店物品列表查询** (`showShopChestItemsForm`):
   - 需要立即显示给玩家
   - 查询结果较小
   - 后续需要访问箱子实体（必须在主线程）

## 技术实现细节

### 线程安全

1. **数据库连接**: 
   - SQLite 使用 WAL 模式支持并发读取
   - 使用 `std::recursive_mutex` 保护数据库操作

2. **玩家对象访问**:
   - 异步查询完成后，通过 UUID 重新获取玩家对象
   - 检查玩家是否仍在线
   - 所有 UI 操作都在主线程执行

3. **BlockSource 访问**:
   - 通过 `player->getDimensionBlockSource()` 在主线程重新获取
   - 避免跨线程访问游戏对象

### 错误处理

```cpp
try {
    auto records = future.get();
    // 处理结果
} catch (const std::exception& e) {
    logger.error("异步查询失败: {}", e.what());
    
    // 回调到主线程通知玩家
    ll::thread::ServerThreadExecutor::getDefault().execute([...]() {
        auto* player = ll::service::getLevel()->getPlayer(...);
        if (player) {
            player->sendMessage("§c查询失败: " + e_msg);
        }
    });
}
```

### 日志记录

每个异步操作都包含详细的日志记录：

```cpp
logger.debug("开始异步查询 pos({},{},{}) dim {}", pos.x, pos.y, pos.z, dimId);
// ... 查询 ...
logger.debug("异步查询完成，记录数: {}", records.size());
```

## 性能影响

### 预期改进

1. **主线程响应性**: 
   - 查询记录时不再阻塞主线程
   - 玩家操作更流畅

2. **并发处理能力**:
   - 多个玩家同时查询记录不会相互阻塞
   - 利用线程池并行处理

3. **大数据量场景**:
   - 查询大量历史记录时影响最明显
   - 避免长时间的主线程卡顿

### 权衡

1. **代码复杂度**: 增加了异步回调的复杂性
2. **内存使用**: 需要复制查询结果到回调中
3. **延迟**: 玩家看到结果会有轻微延迟（通常不明显）

## 使用建议

### 配置线程池

在 `Sqlite3Wrapper::open()` 之前可以配置线程池大小：

```cpp
db.setThreadPoolSize(4);  // 默认为4个线程
```

### 监控性能

可以通过以下方式监控异步任务：

```cpp
size_t pendingTasks = db.getPendingAsyncTasks();
logger.info("待处理的异步任务数: {}", pendingTasks);
```

### 等待所有任务完成

在插件卸载时：

```cpp
db.waitForAllAsyncTasks();  // 等待所有异步任务完成
```

## 未来优化方向

1. **更多查询异步化**:
   - 商店物品列表查询（需要重构以支持异步）
   - 共享箱子列表查询

2. **批量操作优化**:
   - 使用事务批量插入记录
   - 减少数据库往返次数

3. **缓存策略**:
   - 对频繁查询的数据实现缓存
   - 设置合理的缓存过期时间

4. **连接池**:
   - 考虑使用多个数据库连接
   - 进一步提高并发性能

## 注意事项

1. **玩家离线处理**: 异步查询完成时玩家可能已离线，需要检查
2. **数据一致性**: 涉及金钱交易的操作必须保持同步
3. **错误恢复**: 异步操作失败时要有合适的错误提示
4. **日志记录**: 保持详细的日志以便调试

## 相关文件

- `src/db/Sqlite3Wrapper.h` - 数据库包装类（包含异步接口）
- `src/db/Sqlite3Wrapper.cpp` - 数据库实现
- `src/db/ThreadPool.h` - 线程池实现
- `src/db/ThreadPool.cpp` - 线程池实现
- `src/form/ShopForm.cpp` - 商店表单（已优化）
- `src/form/RecycleForm.cpp` - 回收表单（已优化）
- `docs/AsyncDatabaseUsage.md` - 异步数据库使用指南

## 版本历史

- **2025-01-22**: 初始版本，实现三个关键查询的异步化
  - `showPurchaseRecordsForm`
  - `showViewRecycleCommissionsForm`
  - `showCommissionDetailsForm`
