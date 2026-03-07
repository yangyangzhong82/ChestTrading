#include "LockForm.h"
#include "Config/ConfigManager.h"
#include "PlayerLimitForm.h"
#include "RecycleForm.h"
#include "ShopForm.h"
#include "compat/PermissionCompat.h"
#include "Utils/MoneyFormat.h"
#include "Utils/economy.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/TextService.h"

namespace CT {

void showChestLockForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    bool               isLocked,
    const std::string& ownerUuid,
    ChestType          chestType,
    BlockSource&       region
);

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType);
void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showSetRecycleShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

static void showRemoveChestConfirmForm(Player& player, BlockPos pos, int dimId) {
    ll::form::SimpleForm fm;
    auto&                i18n        = I18nService::getInstance();
    auto&                textService = TextService::getInstance();
    bool                 isEn        = (i18n.getCurrentLang() == "en_US");

    fm.setTitle(textService.getMessage("form.button_remove_settings"));
    fm.setContent(
        isEn ? "Are you sure you want to remove this chest settings?\nThis action cannot be undone."
             : "你确定要移除该箱子设置吗？\n此操作不可撤销。"
    );

    fm.appendButton(
        textService.getMessage("form.button_remove_settings"),
        "textures/ui/trash_default",
        "path",
        [pos, dimId](Player& p) {
            auto& region = p.getDimensionBlockSource();
            auto  result = ChestService::getInstance().removeChest(pos, dimId, region);
            p.sendMessage(result.message);
        }
    );
    fm.appendButton(
        textService.getMessage("form.button_cancel"),
        "textures/ui/cancel",
        "path",
        [pos, dimId](Player& p) {
            auto& region = p.getDimensionBlockSource();
            auto  info   = ChestService::getInstance().getChestInfo(pos, dimId, region);
            showChestLockForm(
                p,
                pos,
                dimId,
                info.has_value(),
                info ? info->ownerUuid : "",
                info ? info->type : ChestType::Invalid,
                region
            );
        }
    );
    fm.sendTo(player);
}

void showChestLockForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    bool               isLocked,
    const std::string& ownerUuid,
    ChestType          chestType,
    BlockSource&       region
) {
    ll::form::SimpleForm fm;
    std::string          player_uuid  = player.getUuid().asString();
    bool                 isAdmin      = PermissionCompat::hasPermission(player_uuid, "chest.admin");
    auto&                textService  = TextService::getInstance();

    if (isLocked) {
        // 箱子已设置
        if (ownerUuid == player_uuid || isAdmin) {
            // 当前玩家是主人或管理员
            std::string typeStr = textService.getChestTypeName(chestType);
            std::string content;
            if (isAdmin && ownerUuid != player_uuid && !ownerUuid.empty()) {
                std::string ownerName = ownerUuid;
                if (auto info = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid))) {
                    ownerName = info->name;
                }
                content += textService.getMessage(
                    "form.chest_manage_admin_content",
                    {
                        {"owner", ownerName},
                        {"type",  typeStr   }
                    }
                );
                content += "\n";
            }

            fm.setTitle(textService.getMessage("form.chest_manage_title"));
            content += textService.getMessage(
                "form.chest_manage_content",
                {
                    {"type", typeStr}
                }
            );
            fm.setContent(content);

            fm.appendButton(
                textService.getMessage("form.button_remove_settings"),
                "textures/ui/trash_default",
                "path",
                [pos, dimId](Player& p) {
                    showRemoveChestConfirmForm(p, pos, dimId);
                }
            );

            if (chestType == ChestType::Locked) {
                fm.appendButton(
                    textService.getMessage("form.button_share_chest"),
                    "textures/ui/FriendsIcon",
                    "path",
                    [pos, dimId, ownerUuid](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShareForm(p, pos, dimId, ownerUuid, region);
                    }
                );
            }

            if (chestType == ChestType::Shop || chestType == ChestType::AdminShop) {
                fm.appendButton(
                    textService.getMessage("form.button_manage_shop"),
                    "textures/ui/store_home_icon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopChestManageForm(p, pos, dimId, region);
                    }
                );
                fm.appendButton(
                    textService.getMessage("form.button_set_shop_name"),
                    "textures/ui/editIcon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showSetShopNameForm(p, pos, dimId, region);
                    }
                );
                fm.appendButton(
                    I18nService::getInstance().get("limit.manage_btn"),
                    "textures/ui/permissions_member_star",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showPlayerLimitForm(p, pos, dimId, region, true);
                    }
                );
            } else if (chestType == ChestType::RecycleShop || chestType == ChestType::AdminRecycle) {
                fm.appendButton(
                    textService.getMessage("form.button_add_commission"),
                    "textures/ui/trade_icon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showAddItemToRecycleShopForm(p, pos, dimId, region);
                    }
                );
                fm.appendButton(
                    textService.getMessage("form.button_view_commission"),
                    "textures/ui/book_edit_default",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showViewRecycleCommissionsForm(p, pos, dimId, region);
                    }
                );
                fm.appendButton(
                    textService.getMessage("form.button_set_shop_name"),
                    "textures/ui/editIcon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showSetRecycleShopNameForm(p, pos, dimId, region);
                    }
                );
                fm.appendButton(
                    I18nService::getInstance().get("limit.manage_btn"),
                    "textures/ui/permissions_member_star",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showPlayerLimitForm(p, pos, dimId, region, false);
                    }
                );
            }

            fm.appendButton(
                textService.getMessage("form.button_chest_settings"),
                "textures/ui/settings_glyph_color_2x",
                "path",
                [pos, dimId, chestType](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showChestSettingsForm(p, pos, dimId, region, chestType);
                }
            );

        } else {
            // 当前玩家不是主人
            fm.setTitle(textService.getMessage("form.chest_locked_title"));
            fm.setContent(textService.getMessage("form.chest_locked_content"));

            if (chestType == ChestType::Shop || chestType == ChestType::AdminShop) {
                fm.appendButton(
                    textService.getMessage("form.button_browse_shop"),
                    "textures/ui/store_home_icon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopChestItemsForm(p, pos, dimId, region);
                    }
                );
            } else if (chestType == ChestType::RecycleShop || chestType == ChestType::AdminRecycle) {
                fm.appendButton(
                    textService.getMessage("form.button_browse_recycle"),
                    "textures/ui/trade_icon",
                    "path",
                    [pos, dimId](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showRecycleForm(p, pos, dimId, region);
                    }
                );
            }
        }
    } else {
        // 箱子未设置，提供设置选项
        fm.setTitle(textService.getMessage("form.chest_setup_title"));
        fm.setContent(textService.getMessage("form.chest_setup_content"));

        auto createChestHandler =
            [](Player& p, BlockPos pos, int dimId, const std::string& playerUuid, ChestType type, double cost) {
                auto& region      = p.getDimensionBlockSource();
                auto& svc         = ChestService::getInstance();
                auto& textService = TextService::getInstance();

                std::string errorMsg;
                if (!svc.canPlayerCreateChest(playerUuid, type, errorMsg)) {
                    p.sendMessage(errorMsg);
                    return;
                }
                // 官方商店不检查费用
                bool isAdminType = (type == ChestType::AdminShop || type == ChestType::AdminRecycle);
                if (!isAdminType && cost > 0) {
                    if (!Economy::hasMoney(p, cost)) {
                        p.sendMessage(textService.getMessage(
                            "economy.insufficient",
                            {
                                {"price", MoneyFormat::format(cost)}
                        }
                        ));
                        return;
                    }
                    if (!Economy::reduceMoney(p, cost)) {
                        p.sendMessage(textService.getMessage("economy.deduct_fail"));
                        return;
                    }
                }
                auto result = svc.createChest(playerUuid, pos, dimId, type, region);
                if (result.success) {
                    p.sendMessage(textService.getMessage(
                        "chest.create_success",
                        {
                            {"type",  textService.getChestTypeName(type)},
                            {"price", MoneyFormat::format(cost)         }
                    }
                    ));
                } else {
                    if (!isAdminType && cost > 0) {
                        Economy::addMoney(p, cost);
                    }
                    p.sendMessage(textService.getMessage(
                        "chest.create_fail",
                        {
                            {"type", textService.getChestTypeName(type)}
                    }
                    ));
                }
            };

        fm.appendButton(
            textService.getMessage("form.button_lock_normal"),
            "textures/ui/lock_color",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(
                    p,
                    pos,
                    dimId,
                    player_uuid,
                    ChestType::Locked,
                    ConfigManager::getInstance().get().chestCosts.lockedChestCost
                );
            }
        );

        fm.appendButton(
            textService.getMessage("form.button_set_recycle"),
            "textures/ui/trade_icon",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(
                    p,
                    pos,
                    dimId,
                    player_uuid,
                    ChestType::RecycleShop,
                    ConfigManager::getInstance().get().chestCosts.recycleShopCost
                );
            }
        );

        fm.appendButton(
            textService.getMessage("form.button_set_shop"),
            "textures/ui/store_home_icon",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(
                    p,
                    pos,
                    dimId,
                    player_uuid,
                    ChestType::Shop,
                    ConfigManager::getInstance().get().chestCosts.shopCost
                );
            }
        );

        fm.appendButton(
            textService.getMessage("form.button_set_public"),
            "textures/ui/world_glyph_color_2x",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(
                    p,
                    pos,
                    dimId,
                    player_uuid,
                    ChestType::Public,
                    ConfigManager::getInstance().get().chestCosts.publicChestCost
                );
            }
        );

        // 官方商店按钮（需要权限）
        fm.appendButton(
            textService.getMessage("form.button_set_admin_shop"),
            "textures/ui/permissions_op_crown",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(p, pos, dimId, player_uuid, ChestType::AdminShop, 0.0);
            }
        );

        fm.appendButton(
            textService.getMessage("form.button_set_admin_recycle"),
            "textures/ui/permissions_op_crown",
            "path",
            [pos, dimId, player_uuid, createChestHandler](Player& p) {
                createChestHandler(p, pos, dimId, player_uuid, ChestType::AdminRecycle, 0.0);
            }
        );
    }

    fm.appendButton(
        textService.getMessage("form.button_cancel"),
        "textures/ui/cancel",
        "path",
        [player_uuid](Player& p) { logger.debug("玩家 {} 取消了操作。", player_uuid); }
    );

    fm.sendTo(player);
}

void showChestSettingsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, ChestType chestType) {
    ll::form::CustomForm fm;
    auto&                textService = TextService::getInstance();
    fm.setTitle(textService.getMessage("form.chest_settings_title"));

    auto& chestService = ChestService::getInstance();
    auto  config       = chestService.getChestConfig(pos, dimId, region);

    fm.appendToggle(
        "enable_floating_text",
        textService.getMessage("form.toggle_floating_text"),
        config.enableFloatingText
    );

    bool isShopType =
        (chestType == ChestType::Shop || chestType == ChestType::RecycleShop || chestType == ChestType::AdminShop
         || chestType == ChestType::AdminRecycle);
    if (isShopType) {
        fm.appendToggle("enable_fake_item", textService.getMessage("form.toggle_fake_item"), config.enableFakeItem);
        fm.appendToggle("is_public", textService.getMessage("form.toggle_public"), config.isPublic);
    }

    fm.sendTo(
        player,
        [pos,
         dimId,
         chestType,
         isShopType](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& region      = p.getDimensionBlockSource();
            auto& svc         = ChestService::getInstance();
            auto& textService = TextService::getInstance();

            if (!result.has_value()) {
                p.sendMessage(textService.getMessage("action.cancelled"));
                auto info = svc.getChestInfo(pos, dimId, region);
                showChestLockForm(
                    p,
                    pos,
                    dimId,
                    info.has_value(),
                    info ? info->ownerUuid : "",
                    info ? info->type : ChestType::Invalid,
                    region
                );
                return;
            }

            ChestConfigData newConfig;

            auto ftIt = result->find("enable_floating_text");
            if (ftIt != result->end() && std::holds_alternative<uint64>(ftIt->second)) {
                newConfig.enableFloatingText = (std::get<uint64>(ftIt->second) != 0);
            }

            if (isShopType) {
                auto fiIt = result->find("enable_fake_item");
                if (fiIt != result->end() && std::holds_alternative<uint64>(fiIt->second)) {
                    newConfig.enableFakeItem = (std::get<uint64>(fiIt->second) != 0);
                }
                auto ipIt = result->find("is_public");
                if (ipIt != result->end() && std::holds_alternative<uint64>(ipIt->second)) {
                    newConfig.isPublic = (std::get<uint64>(ipIt->second) != 0);
                }
            }

            if (svc.updateChestConfig(pos, dimId, region, newConfig)) {
                p.sendMessage(textService.getMessage("chest.config_saved"));
            } else {
                p.sendMessage(textService.getMessage("chest.config_fail"));
            }

            auto info = svc.getChestInfo(pos, dimId, region);
            showChestLockForm(
                p,
                pos,
                dimId,
                info.has_value(),
                info ? info->ownerUuid : "",
                info ? info->type : ChestType::Invalid,
                region
            );
        }
    );
}

} // namespace CT
