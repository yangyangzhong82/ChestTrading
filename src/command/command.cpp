#include "command.h"
#include "Bedrock-Authority/permission/PermissionManager.h"
#include "Config/ConfigManager.h"
#include "form/AdminForm.h"
#include "form/PublicShopForm.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "service/I18nService.h"
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
    auto& registrar = CommandRegistrar::getInstance();
    auto& i18n      = I18nService::getInstance();


    auto& ctCmd = registrar.getOrCreateCommand("ct", i18n.get("command.ct_description"), CommandPermissionLevel::Any);

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

    auto& adminCmd =
        registrar.getOrCreateCommand("ctadmin", i18n.get("command.admin_description"), CommandPermissionLevel::Any);

    adminCmd.overload<ll::command::EmptyParam>().execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }
            if (!BA::permission::PermissionManager::getInstance()
                     .hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }
            showAdminMainForm(*player);
        }
    );

    // 注册 /ctreload 命令 - 重新加载配置
    auto& reloadCmd = registrar.getOrCreateCommand(
        "ctreload",
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
        registrar.getOrCreateCommand("shop", i18n.get("command.shop_description"), CommandPermissionLevel::Any);
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
        registrar.getOrCreateCommand("recycle", i18n.get("command.recycle_description"), CommandPermissionLevel::Any);
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

    // 注册 /packchest 命令 - 打包箱子模式
    auto& packCmd = registrar.getOrCreateCommand(
        "packchest",
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
            if (isInPackChestMode(uuid)) {
                setPackChestMode(uuid, false);
                output.success(i18n.get("command.packchest_exit"));
            } else {
                setPackChestMode(uuid, true);
                output.success(i18n.get("command.packchest_enter"));
            }
        }
    );

    // 注册 /cttest 命令 - 自动化测试（开发者工具）
    auto& testCmd =
        registrar.getOrCreateCommand("cttest", i18n.get("command.test_description"), CommandPermissionLevel::Any);

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
            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            std::string result = testHelper.runQuickTests(*player);
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            std::string result = testHelper.runAllTests(*player);

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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
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

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            player->sendMessage(testHelper.testShopTaxRate(*player));
            output.success("税率测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("boundary").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
                output.error(i18n.get("command.no_permission"));
                return;
            }

            auto& testHelper = Test::TestHelper::getInstance();
            player->sendMessage(testHelper.testShopBoundaryConditions(*player));
            output.success("边界条件测试完成");
        }
    );

    testCmd.overload<TestSubcommand>().text("rollback").execute(
        [&i18n](CommandOrigin const& origin, CommandOutput& output, TestSubcommand const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error(i18n.get("command.player_only"));
                return;
            }

            if (!BA::permission::PermissionManager::getInstance().hasPermission(player->getUuid().asString(), "chest.admin")) {
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
