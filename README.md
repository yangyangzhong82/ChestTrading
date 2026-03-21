# ChestTrading

ChestTrading 是一款面向 Minecraft Bedrock 服务器的箱子交易插件，围绕箱子提供完整的商店、回收、公开浏览和交易记录能力。玩家可以直接在游戏内创建和管理店铺，用表单完成上架、购买、回收和检索；服主则可以通过权限、管理员商店、限购和名称限制等功能维护服务器经济秩序。

插件的目标不是把交易做成一组命令，而是把交易系统真正嵌进游戏流程里，让玩家看得见、找得到、买得到，也让管理员更容易控制和扩展。

## 功能特性

- 支持商店、回收商店、上锁箱子、公共箱子等多种箱子类型
- 支持公开商店列表、公开商品列表、按玩家浏览店铺
- 支持商品搜索、玩家搜索、交易记录查询和全服交易动态
- 支持交易记录自动清理，可按总条数和保留天数裁剪
- 支持销量统计与排行榜
- 支持官方商店 / 官方回收商店
- 支持店铺打包与恢复，方便迁移和调整布局
- 支持店铺名称限制，可配置最大长度和禁用关键词
- 支持交易税率、创建费用、数量限制、传送费用与冷却
- 支持普通箱子和铜箱子识别
- 支持多语言与物品贴图资源加载

## 适用场景

- 生存服玩家集市
- 公共交易大厅 / 商业街
- 管理员官方商店
- 资源回收系统
- 需要可视化交易表单的经济服务器

## 依赖与兼容

- LeviLamina `1.9.2`
- SQLite3
- LegacyMoney
- 可选经济依赖：`czmoney`
- 可选权限依赖：`Bedrock-Authority`
- 可选领地依赖：`PLand`

## 核心功能概览

### 店铺系统

- 玩家可创建并管理自己的商店和回收商店
- 支持商品定价、库存同步、交易记录查看
- 支持按玩家查看店铺，并显示店铺在售物品数 / 委托数与销量
- 支持公开商品列表搜索

### 交易记录

- 支持个人近期交易记录
- 支持全服动态交易记录
- 支持查看指定玩家的交易记录
- 支持模糊搜索玩家名，并可直接从在线玩家下拉框选择
- 支持按交易类型、关键词、是否官方商店进行筛选

### 管理能力

- 支持管理员商店 / 官方回收商店
- 支持重载配置
- 支持限购窗口重置
- 支持箱子打包为物品并恢复
- 支持店铺名称审核规则

## 安装

### 1. 准备依赖

确保服务器已安装与当前构建版本匹配的依赖：

- LeviLamina
- LegacyMoney
- 如果需要启用权限节点校验：`Bedrock-Authority`
- 如果需要使用 `czmoney` 经济接口：`czmoney`

### 2. 放入插件文件

将构建产物或发布包放入服务器插件目录。插件启用后会自动：

- 注册命令
- 加载语言文件
- 加载物品贴图映射
- 初始化 SQLite 数据库

默认资源路径使用：

- 数据库：`plugins/ChestTrading/data/ChestTrading.db`
- 语言目录：`plugins/ChestTrading/lang`
- 贴图文件：
  - `plugins/ChestTrading/icon/texture_path.json`
  - `plugins/ChestTrading/icon/terrain_texture.json`
  - `plugins/ChestTrading/icon/item_texture.json`

### 3. 首次启动

首次启动时，如果配置文件不存在，插件会自动生成默认配置。  
如果后续版本新增配置项，插件会在日志中提示缺失的键，但不会强制覆盖你的现有配置文件。

## 快速开始

玩家常用命令：

- `/shop`：打开公开商店列表
- `/recycle`：打开公开回收商店列表
- `/items`：打开公开商店物品列表
- `/recycleitems`：打开公开回收商店物品列表
- `/players`：按玩家浏览商店
- `/recycleplayers`：按玩家浏览回收商店
- `/ranking`：查看销量榜单
- `/records`：打开交易记录中心

管理员常用命令：

- `/ctadmin`：打开管理员菜单
- `/ctreload`：重载配置
- `/ctimportshop merge <x> <y> <z> <file>`：合并导入外部官方商店配置到指定官方商店箱子
- `/ctimportshop replace <x> <y> <z> <file>`：清空后导入外部官方商店配置到指定官方商店箱子
- `/ctlimitreset ...`：重置限购窗口
- `/packchest`：进入 / 退出打包箱子模式

完整命令与权限说明见：

- [docs/COMMANDS_AND_PERMISSIONS.md](./docs/COMMANDS_AND_PERMISSIONS.md)

## 配置说明

插件配置支持注释 JSON，并会对缺失键使用默认值。

常见配置项包括：

- `economyType`：经济实现类型
- `commandSettings`：插件命令名称配置
- `floatingText`：悬浮字与假掉落物显示
- `chestLimits`：各类箱子数量上限
- `chestCosts`：创建费用
- `chestRemovalRefunds`：移除设置时返还费用
- `teleportSettings`：传送费用和冷却
- `taxSettings`：商店税率 / 回收税率
- `interactionSettings`：管理工具、交互防抖、是否要求下蹲
- `formSettings`：表单分页等显示设置
- `salesRankingSettings`：排行榜显示数量
- `shopNameRestrictions`：商店名称限制
- `tradeRestrictionSettings`：禁止上架 / 禁止回收委托的物品列表
- `landRestrictionSettings`：PLand 领地相关限制
- `tradeRecordCleanupSettings`：交易记录自动清理

示例：

```json
{
  "economyType": 0,
  "commandSettings": {
    "mainCommand": "ct",
    "publicShopCommand": "shop",
    "publicRecycleCommand": "recycle"
  },
  "taxSettings": {
    "shopTaxRate": 0.05,
    "recycleTaxRate": 0.02
  },
  "teleportSettings": {
    "enableChestTeleport": true,
    "teleportCost": 100.0,
    "teleportCooldownSec": 60
  },
  "interactionSettings": {
    "manageToolItem": "minecraft:stick",
    "requireSneakingForManage": false
  },
  "formSettings": {
    "publicItemsPerPage": 10
  },
  "shopNameRestrictions": {
    "maxLength": 32,
    "blockedKeywords": [
      "官方",
      "管理",
      "test"
    ]
  },
  "tradeRestrictionSettings": {
    "blockedShopItems": [
      "minecraft:command_block",
      "barrier"
    ],
    "blockedRecycleItems": [
      "minecraft:bedrock"
    ]
  },
  "chestRemovalRefunds": {
    "lockedChestRefund": 100.0,
    "publicChestRefund": 50.0,
    "recycleShopRefund": 500.0,
    "shopRefund": 500.0,
    "adminShopRefund": 0.0,
    "adminRecycleRefund": 0.0
  },
  "landRestrictionSettings": {
    "onlyAllowTradeShopCreationInPland": true,
    "allowAutomationTransferInOwnerPland": true
  },
  "tradeRecordCleanupSettings": {
    "maxTotalRecords": 5000,
    "maxRecordAgeDays": 30
  }
}
```

店铺名称限制说明：

- `maxLength`：按 UTF-8 字符数限制最大长度，`<= 0` 表示不限制
- `blockedKeywords`：按子串匹配，命中任意关键词时拒绝修改名称

表单显示配置说明：

- `publicItemsPerPage`：公开商店物品列表 / 公开回收物品列表表单每页显示数量，`<= 0` 时按 `1` 处理

传送配置说明：

- `enableChestTeleport`：是否允许普通玩家通过公开列表 / 交易记录传送到箱子
- `teleportCost`：每次传送消耗的费用
- `teleportCooldownSec`：传送冷却时间（秒）
- 拥有 `chest.admin` 的管理员不受 `enableChestTeleport` 开关限制

交易物品限制说明：

- `blockedShopItems`：禁止设置为商店商品的物品 ID，按命名空间 ID 精确匹配
- `blockedRecycleItems`：禁止设置为回收委托的物品 ID，按命名空间 ID 精确匹配
- 配置里可写 `diamond_sword`，插件会自动按 `minecraft:diamond_sword` 处理
- 拥有 `chest.admin` 的管理员不受这些限制

移除设置返还说明：

- `lockedChestRefund` / `publicChestRefund` / `recycleShopRefund` / `shopRefund`：移除对应箱子设置时返还给箱子主人的金额
- `adminShopRefund` / `adminRecycleRefund`：移除官方商店 / 官方回收商店设置时返还给箱子主人的金额
- 所有返还金额默认都是 `0`，表示关闭该类型返还
- 返还只在“管理表单 -> 移除设置”时触发，不影响直接破坏箱子的处理逻辑

领地限制说明：

- `onlyAllowTradeShopCreationInPland`：开启后，普通玩家只能在 PLand 领地内创建商店和回收商店
- `allowAutomationTransferInOwnerPland`：开启后，如果受保护箱子位于“箱子主人自己的 PLand 领地”内，则允许漏斗和合成器转移物品
- `chest.admin` 管理员不受此限制
- 若服务器未安装 PLand，或 PLand 对接不可用，则该限制不会生效
- “自己的领地”按箱子主人在该位置的 PLand 权限类型为 `Owner` 判定，成员领地 / 访客领地不会放行

交易记录自动清理说明：

- `maxTotalRecords`：购买记录 + 回收记录的总保留条数，超过后自动删除最旧记录，`< 0` 表示关闭此项清理
- `maxRecordAgeDays`：自动删除早于指定天数的购买/回收记录，`< 0` 表示关闭此项清理
- 清理会在插件启动时执行一次，并在每次交易成功后自动再执行一次

官方商店导入说明：

- 支持导入包含 `purchaseItems` 的外部 JSON 配置，例如 `OfficialShop.json`
- 导入目标必须是当前维度中的“官方商店”箱子
- `merge` 模式会覆盖同商品价格并保留未出现在文件中的旧商品
- `replace` 模式会先清空该官方商店现有商品，再导入文件中的商品
- 路径含空格时请使用英文双引号包裹

## 数据与存储

插件使用 SQLite 作为主要存储，包含但不限于以下数据：

- 箱子基础信息
- 商店商品数据
- 回收委托数据
- 购买记录
- 回收记录
- 动态价格配置

默认启用：

- WAL 模式
- 数据库忙等待超时
- 查询缓存
- 数据库线程池

这些行为可通过配置调整。

## 文档

- [命令与权限说明](./docs/COMMANDS_AND_PERMISSIONS.md)
- [改进计划](./docs/IMPROVEMENT_PLAN.md)

## License

本项目使用仓库内提供的许可证，详见 [LICENSE](./LICENSE)。
