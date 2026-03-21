# ChestTrading 命令与权限节点

本文档基于当前源码整理：
- 命令注册：`src/command/command.cpp`
- 权限校验：`src/compat/PermissionCompat.h`、`src/compat/PermissionCompat.cpp`、`src/service/ChestService.cpp`

说明：
- 以下命令名为默认值，现可通过配置文件中的 `commandSettings` 修改。

## 1. 可用命令

| 命令 | 参数/子命令 | 用途 | 执行限制 |
| --- | --- | --- | --- |
| `/ct` | 无 | 显示插件欢迎信息 | 仅玩家 |
| `/ctadmin` | 无 | 打开管理员主菜单 | 仅玩家；需要管理员权限 |
| `/ctreload` | 无 | 重载配置文件 | 可控制台/玩家执行；命令级别为 `GameDirectors` |
| `/ctimportshop` | 无 | 显示官方商店导入命令用法 | 仅玩家；需要管理员权限 |
| `/ctimportshop merge <x> <y> <z> <file_path>` | `merge` 子命令 | 合并导入外部官方商店配置到指定官方商店箱子 | 仅玩家；需要管理员权限 |
| `/ctimportshop replace <x> <y> <z> <file_path>` | `replace` 子命令 | 清空原商品后导入外部官方商店配置到指定官方商店箱子 | 仅玩家；需要管理员权限 |
| `/shop` | 无 | 打开公开商店列表 | 仅玩家 |
| `/recycle` | 无 | 打开公开回收商店列表 | 仅玩家 |
| `/items` | 无 | 打开公开商店物品列表 | 仅玩家 |
| `/recycleitems` | 无 | 打开公开回收商店物品列表 | 仅玩家 |
| `/ranking` | 无 | 打开销量榜单 | 仅玩家 |
| `/players` | 无 | 按玩家浏览商店 | 仅玩家 |
| `/recycleplayers` | 无 | 按玩家浏览回收商店 | 仅玩家 |
| `/records` | 无 | 打开全服交易记录表单 | 仅玩家 |
| `/packchest` | 无 | 切换“打包箱子模式”开关 | 仅玩家；需要 `chest.pack` |
| `/ctlimitreset` | 无 | 显示重置限购用法说明 | 仅玩家；需要管理员权限 |
| `/ctlimitreset shop <x> <y> <z> [item_id]` | `shop` 子命令 | 重置商店限购窗口（可选指定商品） | 仅玩家；需要管理员权限 |
| `/ctlimitreset recycle <x> <y> <z> [item_id]` | `recycle` 子命令 | 重置回收限购窗口（可选指定商品） | 仅玩家；需要管理员权限 |
| `/ctlimitreset all <x> <y> <z> [item_id]` | `all` 子命令 | 同时重置商店+回收限购窗口（可选指定商品） | 仅玩家；需要管理员权限 |
| `/cttest` | 无 | 显示测试命令菜单 | 仅玩家；需要管理员权限 |
| `/cttest quick` | `quick` 子命令 | 快速测试 | 仅玩家；需要管理员权限 |
| `/cttest shop` | `shop` 子命令 | 商店完整测试 | 仅玩家；需要管理员权限 |
| `/cttest recycle` | `recycle` 子命令 | 回收完整测试 | 仅玩家；需要管理员权限 |
| `/cttest all` | `all` 子命令 | 运行全部测试 | 仅玩家；需要管理员权限 |
| `/cttest cleanup` | `cleanup` 子命令 | 清理测试箱子 | 仅玩家；需要管理员权限 |
| `/cttest money` | `money` 子命令 | 金币不足测试 | 仅玩家；需要管理员权限 |
| `/cttest stock` | `stock` 子命令 | 库存不足测试 | 仅玩家；需要管理员权限 |
| `/cttest tax` | `tax` 子命令 | 税率测试 | 仅玩家；需要管理员权限 |
| `/cttest boundary` | `boundary` 子命令 | 边界条件测试 | 仅玩家；需要管理员权限 |
| `/cttest rollback` | `rollback` 子命令 | 回滚机制测试 | 仅玩家；需要管理员权限 |

## 2. 权限节点清单

以下为插件源码中实际使用的权限节点：

| 权限节点 | 作用 |
| --- | --- |
| `chest.admin` | 管理员总权限。用于 `/ctadmin`、`/ctimportshop`、`/ctlimitreset`、`/cttest`，并在多处交互流程中识别管理员身份。 |
| `chest.create.locked` | 允许创建“上锁箱子”。 |
| `chest.create.public` | 允许创建“公共箱子”。 |
| `chest.create.recycle` | 允许创建“回收商店”。 |
| `chest.create.shop` | 允许创建“商店”。 |
| `chest.create.adminshop` | 允许创建“官方商店”。 |
| `chest.create.adminrecycle` | 允许创建“官方回收商店”。 |
| `chest.pack` | 允许进入打包箱子模式，并执行打包/恢复已打包箱子。 |

## 3. 重要说明

- 命令级别（`CommandPermissionLevel`）和权限节点是两套检查：
  - 例如 `/ctadmin` 既要求命令级别为 `GameDirectors`，也要求 `chest.admin`。
- 创建箱子的权限由业务逻辑校验（`ChestService::canPlayerCreateChest`），不是独立聊天命令。
- 当服务器未安装 `Bedrock-Authority`，或其导出符号不可用时，`PermissionCompat::hasPermission(...)` 会回退为放行模式，即权限节点不生效。
