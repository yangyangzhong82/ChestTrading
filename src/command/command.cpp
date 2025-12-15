#include "command.h"
#include "Bedrock-Authority/permission/PermissionManager.h"
#include "form/AdminForm.h"
#include "form/PublicShopForm.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"


namespace CT {

using ll::command::CommandHandle;
using ll::command::CommandRegistrar;

void registerCommand() {
    auto& registrar = CommandRegistrar::getInstance();
    

    auto& ctCmd =
        registrar.getOrCreateCommand("ct", "ChestTrading 主命令", CommandPermissionLevel::Any);

    ctCmd.overload<ll::command::EmptyParam>().execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error("该命令只能由玩家执行。");
                return;
            }
            output.success("欢迎使用 ChestTrading 插件！");
        }
    );

    auto& adminCmd = registrar.getOrCreateCommand("ctadmin", "ChestTrading 管理员命令", CommandPermissionLevel::Any);

    adminCmd.overload<ll::command::EmptyParam>().execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error("该命令只能由玩家执行。");
                return;
            }
            if (!BA::permission::PermissionManager::getInstance()
                     .hasPermission(player->getUuid().asString(), "czessentials.command.ctadmin")) {
                output.error("你没有权限执行此命令。");
                return;
            }
            showAdminMainForm(*player);
        }
    );

    // 注册 /shop 命令 - 打开公开商店列表
    auto& shopCmd = registrar.getOrCreateCommand("shop", "查看公开商店列表", CommandPermissionLevel::Any);
    shopCmd.overload<ll::command::EmptyParam>().execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error("该命令只能由玩家执行。");
                return;
            }
            showPublicShopListForm(*player);
        }
    );

    // 注册 /recycle 命令 - 打开公开回收商店列表
    auto& recycleCmd = registrar.getOrCreateCommand("recycle", "查看公开回收商店列表", CommandPermissionLevel::Any);
    recycleCmd.overload<ll::command::EmptyParam>().execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::EmptyParam const&, class Command const&) {
            auto* player = static_cast<Player*>(static_cast<PlayerCommandOrigin const&>(origin).getEntity());
            if (!player) {
                output.error("该命令只能由玩家执行。");
                return;
            }
            showPublicRecycleShopListForm(*player);
        }
    );
}

} // namespace CT
