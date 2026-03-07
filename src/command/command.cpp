#include "command.h"
#include "Config/ConfigManager.h"
#include "compat/PermissionCompat.h"
#include "chestui/chestui.h"
#include "form/AdminForm.h"
#include "form/PublicItemsForm.h"
#include "form/PublicShopForm.h"
#include "form/SalesRankingForm.h"
#include "form/TradeRecordForm.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/PlayerLimitService.h"
#include "test/TestHelper.h"
#include <mutex>
#include <set>



namespace CT {

// 打包箱子模式的玩家集合
std::set<std::string> packChestPlayers;
std::mutex            packChestMutex;

bool isInPackChestMode(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(packChestMutex);
    return packChestPlayers.count(uuid) > 0;
}

void setPackChestMode(const std::string& uuid, bool enabled) {
    std::lock_guard<std::mutex> lock(packChestMutex);
    if (enabled) {
        packChestPlayers.insert(uuid);
    } else {
        packChestPlayers.erase(uuid);
    }
}

using ll::command::CommandHandle;
using ll::command::CommandRegistrar;

void registerCommand() {
    auto& registrar = CommandRegistrar::getInstance(false);
    auto& i18n      = I18nService::getInstance();
    auto& config    = ConfigManager::getInstance().get();
    auto& commands  = config.commandSettings;


    auto& ctCmd =
        registrar.getOrCreateCommand(commands.mainCommand, i18n.get("command.ct_description"), CommandPermissionLevel::Any);

    ctCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            output.success(i18n.get("command.ct_welcome"));
        }
    );

    auto& adminCmd = registrar.getOrCreateCommand(
        commands.adminCommand,
        i18n.get("command.admin_description"),
        CommandPermissionLevel::GameDirectors
    );

    adminCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }
            showAdminMainForm(*player);
        }
    );

    // 注册 /ctreload 命令 - 重新加载配置
    auto& reloadCmd = registrar.getOrCreateCommand(
        commands.reloadCommand,
        i18n.get("command.reload_description"),
        CommandPermissionLevel::GameDirectors
    );
    reloadCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const&, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            if (ConfigManager::getInstance().reload()) {
                output.success(i18n.get("command.reload_success"));
            } else {
                output.error(i18n.get("command.reload_fail"));
            }
        }
    );

    // 注册 /shop 命令 - 打开公开商店列表
    auto& shopCmd =
        registrar.getOrCreateCommand(commands.publicShopCommand, i18n.get("command.shop_description"), CommandPermissionLevel::Any);
    shopCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showPublicShopListForm(*player);
        }
    );

    // 注册 /recycle 命令 - 打开公开回收商店列表
    auto& recycleCmd =
        registrar.getOrCreateCommand(commands.publicRecycleCommand, i18n.get("command.recycle_description"), CommandPermissionLevel::Any);
    recycleCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showPublicRecycleShopListForm(*player);
        }
    );

    // 注册 /items 命令 - 打开公开商店物品列表
    auto& itemsCmd =
        registrar.getOrCreateCommand(commands.publicItemsCommand, i18n.get("command.items_description"), CommandPermissionLevel::Any);
    itemsCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showPublicItemsForm(*player);
        }
    );

    // 注册 /recycleitems 命令 - 打开公开回收商店物品列表
    auto& recycleItemsCmd = registrar.getOrCreateCommand(
        commands.recycleItemsCommand,
        i18n.get("command.recycleitems_description"),
        CommandPermissionLevel::Any
    );
    recycleItemsCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showPublicRecycleItemsForm(*player);
        }
    );

    // 注册 /ranking 命令 - 打开销量榜单
    auto& rankingCmd =
        registrar.getOrCreateCommand(commands.rankingCommand, i18n.get("command.ranking_description"), CommandPermissionLevel::Any);
    rankingCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showSalesRankingForm(*player);
        }
    );

    // 注册 /recycleplayers 命令 - 按玩家浏览回收商店
    auto& recyclePlayersCmd = registrar.getOrCreateCommand(
        commands.recyclePlayersCommand,
        i18n.get("command.recycleplayers_description"),
        CommandPermissionLevel::Any
    );
    recyclePlayersCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showPublicRecycleShopListForm(*player);
        }
    );

    auto& recordsCmd =
        registrar.getOrCreateCommand(commands.recordsCommand, i18n.get("command.records_description"), CommandPermissionLevel::Any);
    recordsCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            showTradeRecordMenuForm(*player);
        }
    );

    // 注册 /packchest 命令 - 打包箱子模式
    auto& packCmd = registrar.getOrCreateCommand(
        commands.packChestCommand,
        i18n.get("command.packchest_description"),
        CommandPermissionLevel::Any
    );
    packCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            std::string uuid = player->getUuid().asString();
            if (!PermissionCompat::hasPermission(uuid, "chest.pack")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }
            if (isInPackChestMode(uuid)) {
                setPackChestMode(uuid, false);
                output.success(i18n.get("command.packchest_exit"));
            } else {
                setPackChestMode(uuid, true);
                output.success(i18n.get("command.packchest_enter"));
            }
        }
    );

    // 注册 /ctchestui 命令 - 假箱子 UI 测试
    auto& chestUiCmd = registrar.getOrCreateCommand(commands.chestUiCommand, "ChestUI test command", CommandPermissionLevel::Any);

    struct ChestUiSubcommand {
        std::string action;
    };

    auto getChestUiCommandPlayer = [](CommandOrigin const& origin) -> Player* {
        return static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
    };

    auto openChestUiDemo = [](Player& player, CommandOutput& output) {
        auto actionTypeToString = [](ItemStackRequestActionType type) -> std::string {
            switch (type) {
            case ItemStackRequestActionType::Take:
                return "Take";
            case ItemStackRequestActionType::Place:
                return "Place";
            case ItemStackRequestActionType::Swap:
                return "Swap";
            case ItemStackRequestActionType::Drop:
                return "Drop";
            case ItemStackRequestActionType::Destroy:
                return "Destroy";
            case ItemStackRequestActionType::Consume:
                return "Consume";
            case ItemStackRequestActionType::Create:
                return "Create";
            default:
                return std::to_string(static_cast<int>(type));
            }
        };
        auto itemDisplayString = [](ItemStack const& item) -> std::string {
            if (item.isNull()) {
                return "Air";
            }

            auto name = item.getName();
            if (name.empty()) {
                name = item.getTypeName();
            }

            return name + " §7(" + item.getTypeName() + ")§r x" + std::to_string(item.mCount);
        };

        ChestUI::OpenRequest req;
        req.title = "ChestUI Test";

        ItemStack item1;
        item1.reinit("minecraft:diamond", 16, 0);
        req.items.push_back(item1);

        ItemStack item2;
        item2.reinit("minecraft:emerald", 32, 0);
        req.items.push_back(item2);

        ItemStack item3;
        item3.reinit("minecraft:gold_ingot", 64, 0);
        req.items.push_back(item3);

        req.closeOnClick = true;
        req.onClick      = [actionTypeToString, itemDisplayString](Player& p, ChestUI::ClickContext const& ctx) {
            p.sendMessage(
                "§a[ChestUI] 点击成功: slot=" + std::to_string(ctx.slot) + ", action="
                + actionTypeToString(ctx.actionType) + ", item=" + itemDisplayString(ctx.item)
            );
            p.sendMessage("§7ChestUI 已关闭。");
        };
        req.onClose      = [](Player&) {};

        if (!ChestUI::open(player, std::move(req))) {
            output.error("无法打开 ChestUI。");
            return;
        }
        output.success("已打开 ChestUI 测试界面，点击物品后会显示槽位和物品信息并自动关闭。");
    };

    chestUiCmd.overload<ll::command::EmptyParam>().execute(
        [openChestUiDemo, getChestUiCommandPlayer](
            CommandOrigin const& origin,
            CommandOutput& output,
            ll::command::EmptyParam const&,
            class Command const&
        ) {
            auto* player = getChestUiCommandPlayer(origin);
            if (!player) {
                output.error("This command can only be used by players.");
                return;
            }
            openChestUiDemo(*player, output);
        }
    );

    chestUiCmd.overload<ChestUiSubcommand>().text("open").execute(
        [openChestUiDemo, getChestUiCommandPlayer](
            CommandOrigin const& origin,
            CommandOutput& output,
            ChestUiSubcommand const&,
            class Command const&
        ) {
            auto* player = getChestUiCommandPlayer(origin);
            if (!player) {
                output.error("This command can only be used by players.");
                return;
            }
            openChestUiDemo(*player, output);
        }
    );

    chestUiCmd.overload<ChestUiSubcommand>().text("close").execute(
        [getChestUiCommandPlayer](CommandOrigin const& origin, CommandOutput& output, ChestUiSubcommand const&, class Command const&) {
            auto* player = getChestUiCommandPlayer(origin);
            if (!player) {
                output.error("This command can only be used by players.");
                return;
            }
            if (ChestUI::close(*player)) {
                output.success("ChestUI 已关闭。");
            } else {
                output.error("当前没有打开的 ChestUI。");
            }
        }
    );

    chestUiCmd.overload<ChestUiSubcommand>().text("state").execute(
        [getChestUiCommandPlayer](CommandOrigin const& origin, CommandOutput& output, ChestUiSubcommand const&, class Command const&) {
            auto* player = getChestUiCommandPlayer(origin);
            if (!player) {
                output.error("This command can only be used by players.");
                return;
            }
            output.success(ChestUI::isOpen(*player) ? "ChestUI: opened" : "ChestUI: closed");
        }
    );

    // 注册 /ctlimitreset 命令 - 手动重置箱子限购窗口
    auto& limitResetCmd = registrar.getOrCreateCommand(
        commands.limitResetCommand,
        i18n.get("command.limit_reset_description"),
        CommandPermissionLevel::Any
    );

    struct LimitResetPosParam {
        int x;
        int y;
        int z;
    };
    struct LimitResetItemParam {
        int x;
        int y;
        int z;
        int item_id;
    };

    auto executeLimitReset = [&i18n](
                                 CommandOrigin const&      origin,
                                 CommandOutput&            output,
                                 LimitResetPosParam const& param,
                                 bool                      resetShop,
                                 bool                      resetRecycle,
                                 int                       itemId,
                                 bool                      specificItem
                             ) {
        auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
        if (!player) {
            output.error(i18n.get("command.player_only"));
            return;
        }

        if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
            output.error(i18n.get("command.no_permission"));
            return;
        }

        if (specificItem && itemId <= 0) {
            output.error(i18n.get("command.limit_reset_invalid_item"));
            return;
        }

        auto& chestService = ChestService::getInstance();
        auto& region       = player->getDimensionBlockSource();
        int   dimId        = static_cast<int>(player->getDimensionId());
        auto  mainPos      = chestService.getMainChestPos(BlockPos{param.x, param.y, param.z}, region);
        auto  chestInfo    = chestService.getChestInfo(mainPos, dimId, region);
        if (!chestInfo) {
            output.error(i18n.get(
                "command.limit_reset_chest_not_found",
                {
                    {"x", std::to_string(mainPos.x)},
                    {"y", std::to_string(mainPos.y)},
                    {"z", std::to_string(mainPos.z)}
            }
            ));
            return;
        }

        bool isShopChest    = chestInfo->type == ChestType::Shop || chestInfo->type == ChestType::AdminShop;
        bool isRecycleChest = chestInfo->type == ChestType::RecycleShop || chestInfo->type == ChestType::AdminRecycle;

        if (resetShop && !resetRecycle && !isShopChest) {
            output.error(i18n.get("command.limit_reset_not_shop"));
            return;
        }
        if (resetRecycle && !resetShop && !isRecycleChest) {
            output.error(i18n.get("command.limit_reset_not_recycle"));
            return;
        }
        if (resetShop && resetRecycle && !isShopChest && !isRecycleChest) {
            output.error(i18n.get("command.limit_reset_not_trade_chest"));
            return;
        }

        auto& limitService = PlayerLimitService::getInstance();
        bool  ok           = true;
        if (resetShop) {
            ok = limitService.resetLimitWindow(mainPos, dimId, true, specificItem ? itemId : 0) && ok;
        }
        if (resetRecycle) {
            ok = limitService.resetLimitWindow(mainPos, dimId, false, specificItem ? itemId : 0) && ok;
        }
        if (!ok) {
            output.error(i18n.get("command.limit_reset_failed"));
            return;
        }

        std::string resetType = i18n.get("command.limit_type_all");
        if (resetShop && !resetRecycle) {
            resetType = i18n.get("command.limit_type_shop");
        } else if (!resetShop && resetRecycle) {
            resetType = i18n.get("command.limit_type_recycle");
        }

        if (specificItem) {
            output.success(i18n.get(
                "command.limit_reset_success_item",
                {
                    {"type",    resetType                },
                    {"item_id", std::to_string(itemId)   },
                    {"x",       std::to_string(mainPos.x)},
                    {"y",       std::to_string(mainPos.y)},
                    {"z",       std::to_string(mainPos.z)},
                    {"dim",     std::to_string(dimId)    }
            }
            ));
        } else {
            output.success(i18n.get(
                "command.limit_reset_success",
                {
                    {"type", resetType                },
                    {"x",    std::to_string(mainPos.x)},
                    {"y",    std::to_string(mainPos.y)},
                    {"z",    std::to_string(mainPos.z)},
                    {"dim",  std::to_string(dimId)    }
            }
            ));
        }
    };

    limitResetCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }
            output.success(i18n.get("command.limit_reset_usage"));
        }
    );

    limitResetCmd.overload<LimitResetPosParam>().text("shop").required("x").required("y").required("z").execute(
        [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetPosParam const& param, class Command const&) {
            executeLimitReset(origin, output, param, true, false, 0, false);
        }
    );

    limitResetCmd.overload<LimitResetPosParam>().text("recycle").required("x").required("y").required("z").execute(
        [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetPosParam const& param, class Command const&) {
            executeLimitReset(origin, output, param, false, true, 0, false);
        }
    );

    limitResetCmd.overload<LimitResetPosParam>().text("all").required("x").required("y").required("z").execute(
        [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetPosParam const& param, class Command const&) {
            executeLimitReset(origin, output, param, true, true, 0, false);
        }
    );

    limitResetCmd.overload<LimitResetItemParam>()
        .text("shop")
        .required("x")
        .required("y")
        .required("z")
        .required("item_id")
        .execute(
            [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetItemParam const& param, class Command const&) {
                executeLimitReset(
                    origin,
                    output,
                    LimitResetPosParam{param.x, param.y, param.z},
                    true,
                    false,
                    param.item_id,
                    true
                );
            }
        );

    limitResetCmd.overload<LimitResetItemParam>()
        .text("recycle")
        .required("x")
        .required("y")
        .required("z")
        .required("item_id")
        .execute(
            [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetItemParam const& param, class Command const&) {
                executeLimitReset(
                    origin,
                    output,
                    LimitResetPosParam{param.x, param.y, param.z},
                    false,
                    true,
                    param.item_id,
                    true
                );
            }
        );

    limitResetCmd.overload<LimitResetItemParam>()
        .text("all")
        .required("x")
        .required("y")
        .required("z")
        .required("item_id")
        .execute(
            [&executeLimitReset](CommandOrigin const& origin, CommandOutput& output, LimitResetItemParam const& param, class Command const&) {
                executeLimitReset(
                    origin,
                    output,
                    LimitResetPosParam{param.x, param.y, param.z},
                    true,
                    true,
                    param.item_id,
                    true
                );
            }
        );

    // 注册 /cttest 命令 - 自动化测试（开发者工具）
    auto& testCmd =
        registrar.getOrCreateCommand(commands.testCommand, i18n.get("command.test_description"), CommandPermissionLevel::Any);

    struct TestSubcommand {
        std::string testType;
    };

    testCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            // 权限检查：只允许管理员使用测试命令
            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            // 显示测试菜单
            player->sendMessage("§e=== ChestTrading 测试工具 ===");
            player->sendMessage("§7箱子会自动放置，无需手动操作\n");
            player->sendMessage("§a/cttest quick      §7- 快速测试（核心功能）");
            player->sendMessage("§a/cttest shop       §7- 测试商店完整功能");
            player->sendMessage("§a/cttest recycle    §7- 测试回收完整功能");
            player->sendMessage("§a/cttest all        §7- 运行所有测试");
            player->sendMessage("§a/cttest cleanup    §7- 清理所有测试箱子\n");
            player->sendMessage("§e--- 细粒度测试 ---");
            player->sendMessage("§a/cttest money      §7- 测试金币不足");
            player->sendMessage("§a/cttest stock      §7- 测试库存不足");
            player->sendMessage("§a/cttest tax        §7- 测试税率");
            player->sendMessage("§a/cttest boundary   §7- 测试边界条件");
            player->sendMessage("§a/cttest rollback   §7- 测试回滚机制");
        }
    );

    testCmd.overload<TestSubcommand>().text("quick").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto&       testHelper = Test::TestHelper::getInstance();
            std::string result     = testHelper.runQuickTests(*player);
            player->sendMessage(result);
            output.success("快速测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("shop").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto&       testHelper = Test::TestHelper::getInstance();
            std::string result;
            result += testHelper.testShopPurchase(*player, true);
            result += "\n" + testHelper.testShopInventorySync(*player);
            result += "\n" + testHelper.testShopPriceUpdate(*player);
            result += "\n" + testHelper.testShopInsufficientMoney(*player);
            result += "\n" + testHelper.testShopInsufficientStock(*player);
            result += "\n" + testHelper.testShopTaxRate(*player);
            result += "\n" + testHelper.testShopBoundaryConditions(*player);

            player->sendMessage(result);
            output.success("商店测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("recycle").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto&       testHelper = Test::TestHelper::getInstance();
            std::string result;
            result += testHelper.testRecycle(*player, true);
            result += "\n" + testHelper.testRecycleFilters(*player);
            result += "\n" + testHelper.testRecycleRollback(*player);
            result += "\n" + testHelper.testRecycleOwnerInsufficientMoney(*player);

            player->sendMessage(result);
            output.success("回收测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("all").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto&       testHelper = Test::TestHelper::getInstance();
            std::string result     = testHelper.runAllTests(*player);

            player->sendMessage(result);
            output.success("所有测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("cleanup").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            testHelper.cleanupAllTestChests(*player);

            player->sendMessage("§a已清理所有测试箱子");
            output.success("清理完成");
        }
    );

    // 细粒度测试命令
    testCmd.overload<TestSubcommand>().text("money").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            player->sendMessage(testHelper.testShopInsufficientMoney(*player));
            output.success("金币不足测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("stock").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            player->sendMessage(testHelper.testShopInsufficientStock(*player));
            output.success("库存不足测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("tax").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            player->sendMessage(testHelper.testShopTaxRate(*player));
            output.success("税率测试完成");
        }
    );

    testCmd.overload<TestSubcommand>()
        .text("boundary")
        .execute(
            [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
                auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
                if (!player) {
                    output.error(i18n.get("command.player_only"));
                    return;
                }

                if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                    output.error(i18n.get("command.no_permission"));
                    return;
                }

                auto& testHelper = Test::TestHelper::getInstance();
                player->sendMessage(testHelper.testShopBoundaryConditions(*player));
                output.success("边界条件测试完成");
            }
        );

    testCmd.overload<TestSubcommand>()
        .text("rollback")
        .execute(
            [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
                auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
                if (!player) {
                    output.error(i18n.get("command.player_only"));
                    return;
                }

                if (!PermissionCompat::hasPermission(player->getUuid().asString(), "chest.admin")) {
                    output.error(i18n.get("command.no_permission"));
                    return;
                }

                auto& testHelper = Test::TestHelper::getInstance();
                player->sendMessage(testHelper.testRecycleRollback(*player));
                output.success("回滚测试完成");
            }
        );
}

} // namespace CT
