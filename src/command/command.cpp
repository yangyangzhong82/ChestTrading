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
    
    // 注册 /ct 命令
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

    // 注册独立的 /ctadmin 命令
    auto& adminCmd = registrar.getOrCreateCommand("ctadmin", "ChestTrading 管理员命令", CommandPermissionLevel::GameDirectors);

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
            // 权限已在命令注册时通过 CommandPermissionLevel::Operator 设置，此处无需再次检查
            showAdminMainForm(*player);
        }
    );

    // 注册 /shop 命令 - 打开公开商店列表
    auto& shopCmd = registrar.getOrCreateCommand("shop", "查看公开商店列表", CommandPermissionLevel::GameDirectors);
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
    auto& recycleCmd =
        registrar.getOrCreateCommand("recycle", "查看公开回收商店列表", CommandPermissionLevel::GameDirectors);
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
