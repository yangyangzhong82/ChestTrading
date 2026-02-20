# ChestTrading

<div align="center">

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![LeviLamina](https://img.shields.io/badge/LeviLamina-1.7.6-green.svg)](https://github.com/LiteLDev/LeviLamina)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()

一个功能强大的 Minecraft 基岩版箱子管理插件，为 LeviLamina 服务器提供箱子上锁、商店系统、回收系统等完整的箱子交易解决方案。

</div>

---

## ✨ 功能特性

### 🔒 箱子管理
- **箱子上锁** - 保护你的物品不被他人盗取
- **权限分享** - 与好友分享箱子访问权限
- **公共箱子** - 创建公开访问的共享箱子
- **大箱子支持** - 自动识别并管理双箱子

### 🏪 商店系统
- **玩家商店** - 在箱子中出售物品，自动收款
- **物品定价** - 灵活设置每个物品的价格
- **库存管理** - 实时追踪商品库存
- **交易记录** - 完整的购买历史记录
- **税率系统** - 可配置的交易税

### ♻️ 回收系统
- **回收商店** - 设置物品回收价格，自动收购
- **精确匹配** - 支持按耐久度、附魔、特殊值过滤
- **回滚机制** - 安全的物品转移与回滚保护
- **批量回收** - 高效处理大量物品

### 🎯 悬浮字显示
- **动态悬浮字** - 箱子上方实时显示箱子信息
- **商品预览** - 商店物品自动轮播展示
- **假物品展示** - 3D 物品模型预览
- **独立开关** - 每个箱子可单独配置显示

### 🌐 多语言支持
- **中文** (zh_CN)
- **英文** (en_US)
- 支持自定义语言包

### 💰 经济系统集成
- **LLMoney** - 支持 LLMoney 经济系统
- **CzMoney** - 支持 CzMoney 经济系统
- 可配置切换

---

## 📦 安装

### 前置依赖

- **LeviLamina** ≥ 1.7.6
- **Bedrock-Authority** 0.2.0（权限系统）
- **debug_shape** 0.5.0（悬浮字）
- **LLMoney** 或 **CzMoney**（二选一）

### 安装步骤

1. 下载最新版本的 `ChestTrading` 插件
2. 将 `ChestTrading` 文件夹放入 `plugins/` 目录
3. 重启服务器
4. 插件会自动生成配置文件在 `plugins/ChestTrading/config.json`

---

## ⚙️ 配置

配置文件位于 `plugins/ChestTrading/config.json`

### 核心配置

```json
{
  "version": 1,
  "economyType": 1,  // 0=LLMoney, 1=CzMoney
  "enableWalMode": true,  // 启用 SQLite WAL 模式
  "busyTimeoutMs": 5000,  // 数据库超时时间（毫秒）
  "logLevel": 2  // 日志等级：0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error
}
```

### 箱子数量限制

```json
{
  "chestLimits": {
    "maxLockedChests": 10,  // 每人最多上锁箱子数
    "maxPublicChests": 5,   // 每人最多公共箱子数
    "maxShops": 3,          // 每人最多商店数
    "maxRecycleShops": 3    // 每人最多回收商店数
  }
}
```

### 箱子创建费用

```json
{
  "chestCosts": {
    "lockedChestCost": 100.0,   // 上锁箱子费用
    "publicChestCost": 50.0,    // 公共箱子费用
    "shopCost": 500.0,          // 商店费用
    "recycleShopCost": 500.0    // 回收商店费用
  }
}
```

### 传送设置

```json
{
  "teleportSettings": {
    "teleportCost": 100.0,      // 传送费用
    "teleportCooldownSec": 60   // 传送冷却（秒）
  }
}
```

### 税率设置

```json
{
  "taxSettings": {
    "shopTaxRate": 0.05,      // 商店交易税率 (5%)
    "recycleTaxRate": 0.05    // 回收交易税率 (5%)
  }
}
```

### 悬浮字配置

```json
{
  "floatingText": {
    "enableLockedChest": true,   // 上锁箱子悬浮字
    "enablePublicChest": true,   // 公共箱子悬浮字
    "enableShopChest": true,     // 商店箱子悬浮字
    "enableRecycleShop": true,   // 回收商店悬浮字
    "enableFakeItem": true       // 假物品展示
  },
  "floatingTextUpdateIntervalSeconds": 1  // 更新间隔（秒）
}
```

### 性能优化

```json
{
  "databaseThreadPoolSize": 4,        // 数据库线程池大小
  "databaseCacheTimeoutSec": 60,      // 数据库缓存超时（秒）
  "interactionSettings": {
    "debounceIntervalMs": 500,        // 交互防抖间隔（毫秒）
    "cleanupThresholdSec": 60         // 记录清理阈值（秒）
  }
}
```

---

## 🎮 使用指南

### 基础操作

1. **对箱子右键** - 打开箱子管理菜单
2. **选择箱子类型**：
   - 上锁箱子：只有你和分享的玩家可以访问
   - 公共箱子：所有人都可以访问
   - 商店：出售物品
   - 回收商店：收购物品

### 商店使用

#### 创建商店
1. 右键点击箱子
2. 选择"设置为商店"
3. 支付创建费用
4. 在箱子中放入要出售的物品
5. 右键箱子，为物品设置价格

#### 购买物品
1. 右键点击商店箱子
2. 浏览可购买的物品
3. 选择数量并购买
4. 物品自动进入背包（背包满时掉落）

### 回收商店使用

#### 设置回收
1. 右键点击箱子
2. 选择"设置为回收商店"
3. 右键箱子，添加回收委托
4. 设置回收价格和过滤条件

#### 出售物品
1. 找到回收商店
2. 右键查看回收列表
3. 选择要出售的物品
4. 确认出售，金钱自动到账

### 权限分享

1. 右键上锁箱子
2. 选择"分享管理"
3. 输入玩家名称
4. 该玩家可以访问此箱子

---

## 🔑 权限节点

使用 Bedrock-Authority 权限系统：

| 权限节点 | 说明 | 默认值 |
|---------|------|--------|
| `chest.admin` | 管理员权限，绕过所有限制 | OP |
| `chest.create.locked` | 创建上锁箱子 | 所有人 |
| `chest.create.public` | 创建公共箱子 | 所有人 |
| `chest.create.shop` | 创建商店 | 所有人 |
| `chest.create.recycle` | 创建回收商店 | 所有人 |

### 配置权限

在 `plugins/Bedrock-Authority/permissions.json` 中配置：

```json
{
  "roles": {
    "vip": {
      "permissions": [
        "chest.create.locked",
        "chest.create.public",
        "chest.create.shop",
        "chest.create.recycle"
      ]
    }
  }
}
```

---

## 📝 命令列表

| 命令 | 说明 | 权限 |
|------|------|------|
| `/ct` | ChestTrading 主命令 | 所有人 |
| `/ctadmin` | 打开管理员面板 | `chest.admin` |
| `/ctreload` | 重新加载配置文件 | OP |
| `/shop` | 查看公开商店列表 | 所有人 |
| `/recycle` | 查看公开回收商店列表 | 所有人 |
| `/packchest` | 打包箱子为物品 | 所有人 |
| `/ctlimitreset <shop\|recycle\|all> <x> <y> <z>` | 手动重置指定箱子限购窗口 | `chest.admin` |
| `/cttest [shop\|recycle\|all]` | 运行自动化测试（开发者工具） | `chest.admin` |

> **提示**：除命令外，大部分功能通过右键箱子的表单界面操作。

### 开发者测试

插件提供了游戏内自动化测试系统，让开发者可以单独一人测试商店和回收功能：

**使用方法：**
1. 在你前方 3 格处放置一个普通箱子
2. 输入测试命令

```bash
/cttest           # 显示测试菜单
/cttest shop      # 测试商店购买功能（自动转换箱子、设置价格、购买、验证）
/cttest recycle   # 测试回收功能（自动转换箱子、设置委托、回收、验证）
/cttest all       # 运行所有测试
```

详细测试文档请查看 [TESTING.md](TESTING.md)

---

## 🔧 开发者信息

### 技术栈

- **语言**: C++20
- **框架**: LeviLamina 1.7.6
- **数据库**: SQLite (WAL 模式)
- **构建工具**: xmake
- **架构**: 分层架构（Repository → Service → Form）

### 项目结构

```
ChestTrading/
├── src/
│   ├── Entry/          # 模组入口
│   ├── command/        # 命令系统
│   ├── Config/         # 配置管理
│   ├── db/             # 数据库层（SQLite、线程池、迁移）
│   ├── repository/     # 数据访问层（DAO）
│   ├── service/        # 业务逻辑层
│   ├── form/           # 表单界面层
│   ├── interaction/    # 事件处理层
│   ├── FloatingText/   # 悬浮字系统
│   └── Utils/          # 工具类
├── lang/               # 多语言文件
├── xmake.lua          # 构建配置
└── README.md          # 项目文档
```

### 数据库表结构

- `chests` - 箱子主表
- `shared_chests` - 箱子分享关系
- `item_definitions` - 物品定义（NBT去重）
- `shop_items` - 商店物品
- `purchase_records` - 购买记录
- `recycle_shop_items` - 回收委托
- `recycle_records` - 回收记录

### 构建项目

```bash
# 安装依赖
xmake repo -u

# 构建
xmake build

# 输出位于 bin/ChestTrading/
```

---

## 🐛 常见问题

### Q: 箱子无法上锁？
A: 检查以下几点：
1. 是否有 `chest.create.locked` 权限
2. 是否达到箱子数量上限
3. 是否有足够的金钱支付创建费用

### Q: 商店物品不显示？
A: 确认：
1. 悬浮字配置已启用（`enableShopChest: true`）
2. 箱子中有物品且已设置价格
3. 单箱子配置中悬浮字未被禁用

### Q: 数据库损坏怎么办？
A:
1. 停止服务器
2. 备份 `plugins/ChestTrading/ChestTrading.db`
3. 删除 `.db-shm` 和 `.db-wal` 文件
4. 重启服务器

### Q: 如何清空某玩家的所有箱子？
A: 使用管理员表单：
1. 右键任意箱子
2. 选择"管理员工具"（需要 `chest.admin` 权限）
3. 筛选玩家箱子
4. 批量删除

---

## 📊 性能优化

本插件经过深度性能优化：

- ✅ **数据库查询批处理** - N+1 查询优化，启动加载速度提升 **77-87%**
- ✅ **协程锁优化** - 锁持有时间减少 **60-80%**
- ✅ **缓存机制** - 箱子信息缓存，减少数据库访问
- ✅ **异步操作** - 线程池处理数据库任务
- ✅ **事务保护** - RAII 风格的事务管理
- ✅ **WAL 模式** - SQLite WAL 模式提升并发性能

### 性能基准

| 场景 | 箱子数量 | 玩家数量 | 启动时间 | 并发TPS |
|------|---------|---------|---------|---------|
| 小型服务器 | <100 | <20 | ~0.8s | 19.8+ |
| 中型服务器 | 100-500 | 20-50 | ~2.5s | 19.5+ |
| 大型服务器 | >500 | >50 | ~5s | 19.0+ |

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

### 贡献指南

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

### 代码规范

- 遵循现有的代码风格
- 添加必要的注释
- 确保所有测试通过
- 更新相关文档

---

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

---

## 🙏 致谢

- [LeviLamina](https://github.com/LiteLDev/LeviLamina) - 基岩版服务器框架
- [Bedrock-Authority](https://github.com/LiteLDev/Bedrock-Authority) - 权限系统
- [debug_shape](https://github.com/engsr6982/debug_shape) - 悬浮字库

---

## 📞 联系方式

- **Issues**: [GitHub Issues](https://github.com/yourusername/ChestTrading/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/ChestTrading/discussions)

---

<div align="center">

**⭐ 如果这个项目对你有帮助，请给个Star！**

Made with ❤️ by ChestTrading Contributors

</div>
