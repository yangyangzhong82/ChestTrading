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
        std::string testType; // all, shop, recycle
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
            player->sendMessage("§a/cttest shop        §7- 测试商店购买功能");
            player->sendMessage("§a/cttest recycle    §7- 测试回收功能");
            player->sendMessage("§a/cttest all        §7- 运行所有测试");
            player->sendMessage("§7提示: 测试会自动创建和清理箱子");
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
            std::string result = testHelper.testShopPurchase(*player, true);
            result += "\n" + testHelper.testShopInventorySync(*player);
            result += "\n" + testHelper.testShopPriceUpdate(*player);

            player->sendMessage(result);
            output.success("商店测试完成，详情请查看聊天窗口");
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
            std::string result = testHelper.testRecycle(*player, true);

            player->sendMessage(result);
            output.success("回收测试完成，详情请查看聊天窗口");
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
            output.success("所有测试完成，详情请查看聊天窗口");
        }
    );
}

} // namespace CT
