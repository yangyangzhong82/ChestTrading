#include "LandChestSettingForm.h"

#include "Types.h"
#include "compat/PLandCompat.h"
#include "compat/PermissionCompat.h"
#include "ll/api/form/CustomForm.h"
#include "repository/LandSettingRepository.h"
#include "service/TextService.h"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace CT {

namespace {
// 普通玩家可创建的箱子类型（官方商店/官方回收为管理员专属，会被更早的管理员判定绕过，故不在此列出）。
constexpr std::array<ChestType, 4> kConfigurableTypes = {
    ChestType::Locked,
    ChestType::Public,
    ChestType::Shop,
    ChestType::RecycleShop
};

std::string toggleKey(ChestType type) { return "type_" + std::to_string(static_cast<int>(type)); }
} // namespace

void showLandChestSettingForm(Player& player) {
    auto& txt   = TextService::getInstance();
    auto& pland = PLandCompat::getInstance();

    BlockPos pos   = player.getFeetBlockPos();
    int      dimId = static_cast<int>(player.getDimensionId());

    auto landId = pland.getLandId(pos, dimId);
    if (!landId.has_value()) {
        player.sendMessage(txt.getMessage("land_setting.pland_unavailable"));
        return;
    }
    if (*landId < 0) {
        player.sendMessage(txt.getMessage("land_setting.not_in_land"));
        return;
    }

    std::string playerUuid   = player.getUuid().asString();
    bool        isChestAdmin = PermissionCompat::hasPermission(playerUuid, "chest.admin");
    bool        isLandOwner  = pland.isLandManager(player, pos).value_or(false);
    if (!isLandOwner && !isChestAdmin) {
        player.sendMessage(txt.getMessage("land_setting.no_permission"));
        return;
    }

    int mask = LandSettingRepository::getInstance().getAllowedTypesMask(*landId).value_or(~0);

    ll::form::CustomForm fm;
    fm.setTitle(txt.getMessage("land_setting.title"));
    fm.appendLabel(txt.getMessage("land_setting.label"));
    for (ChestType type : kConfigurableTypes) {
        bool allowed = (mask & (1 << static_cast<int>(type))) != 0;
        fm.appendToggle(toggleKey(type), txt.getChestTypeName(type), allowed);
    }

    int64_t landIdValue = *landId;
    fm.sendTo(
        player,
        [landIdValue, mask](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& txt = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                return;
            }

            // 从现有掩码出发，仅修改可配置类型对应的位，保留其他位（如官方类型）。
            int newMask = mask;
            for (ChestType type : kConfigurableTypes) {
                int  bit = 1 << static_cast<int>(type);
                auto it  = result->find(toggleKey(type));
                bool on  = (it != result->end()) && std::holds_alternative<uint64>(it->second)
                       && (std::get<uint64>(it->second) != 0);
                if (on) {
                    newMask |= bit;
                } else {
                    newMask &= ~bit;
                }
            }

            if (LandSettingRepository::getInstance().setAllowedTypesMask(landIdValue, newMask)) {
                p.sendMessage(txt.getMessage("land_setting.saved"));
            } else {
                p.sendMessage(txt.getMessage("land_setting.save_fail"));
            }
        }
    );
}

} // namespace CT
