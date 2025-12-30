#include "shareForm.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "service/ChestService.h"
#include "service/TextService.h"
#include <algorithm>
#include <string>
#include <vector>

namespace CT {

// 辅助函数：根据 UUID 获取玩家名称
std::string getPlayerNameFromUuid(const std::string& uuidString) {
    auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuidString));
    if (playerInfo) {
        return playerInfo->name;
    }
    return uuidString; // 如果找不到玩家，返回 UUID 本身
}

// 前向声明，用于在回调中互相调用
void showAddOfflineShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region
);
void showAddShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region,
    int                currentPage
);
void showRemoveShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region,
    int                currentPage
);

void showShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region,
    int                currentPage
) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.share_title"));

    auto&                    chestService  = ChestService::getInstance();
    std::vector<std::string> sharedPlayers = chestService.getSharedPlayers(pos, dimId, region);
    std::string              ownerName     = getPlayerNameFromUuid(ownerUuid);

    std::string content = txt.getMessage(
                              "form.share_owner",
                              {
                                  {"owner", ownerName}
    }
                          )
                        + "\n\n" + txt.getMessage("form.share_list") + "\n";
    if (sharedPlayers.empty()) {
        content += txt.getMessage("form.share_none") + "\n";
    } else {
        for (const std::string& sharedPlayerUuid : sharedPlayers) {
            content += "- " + getPlayerNameFromUuid(sharedPlayerUuid) + "\n";
        }
    }
    fm.setContent(content);

    fm.appendButton(txt.getMessage("form.button_add_online"), [pos, dimId, ownerUuid, currentPage](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddShareForm(p, pos, dimId, ownerUuid, region, currentPage);
    });

    fm.appendButton(txt.getMessage("form.button_add_offline"), [pos, dimId, ownerUuid](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddOfflineShareForm(p, pos, dimId, ownerUuid, region);
    });

    if (!sharedPlayers.empty()) {
        fm.appendButton(txt.getMessage("form.button_remove_share"), [pos, dimId, ownerUuid, currentPage](Player& p) {
            auto& region = p.getDimensionBlockSource();
            showRemoveShareForm(p, pos, dimId, ownerUuid, region, currentPage);
        });
    }

    fm.appendButton(txt.getMessage("form.button_cancel"), [](Player& p) {
        logger.info("玩家 {} 取消了箱子分享管理。", p.getUuid().asString());
    });

    fm.sendTo(player);
}

void showAddOfflineShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region
) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.share_add_offline_title"));
    fm.appendInput(OFFLINE_PLAYER_INPUT_KEY, txt.getMessage("form.input_offline_player"), "");

    fm.sendTo(
        player,
        [pos, dimId, ownerUuid](Player& p, const ll::form::CustomFormResult& res, ll::form::FormCancelReason reason) {
            auto& region = p.getDimensionBlockSource();
            if (!res) {
                logger.debug("玩家 {} 取消了添加离线玩家。", p.getUuid().asString());
                showShareForm(p, pos, dimId, ownerUuid, region); // 返回主菜单
                return;
            }

            if (res->count(OFFLINE_PLAYER_INPUT_KEY)) {
                const auto& offlinePlayerNameResult = res->at(OFFLINE_PLAYER_INPUT_KEY);
                if (std::holds_alternative<std::string>(offlinePlayerNameResult)) {
                    std::string offlinePlayerName = std::get<std::string>(offlinePlayerNameResult);
                    if (!offlinePlayerName.empty()) {
                        auto playerInfo = ll::service::PlayerInfo::getInstance().fromName(offlinePlayerName);
                        if (playerInfo) {
                            std::string offlinePlayerUuid = playerInfo->uuid.asString();
                            auto&       svc               = ChestService::getInstance();
                            auto&       txt               = TextService::getInstance();
                            if (svc.addSharedPlayer(ownerUuid, offlinePlayerUuid, pos, dimId, region)) {
                                p.sendMessage(txt.getMessage(
                                    "share.add_success",
                                    {
                                        {"player", offlinePlayerName}
                                }
                                ));
                                logger.info("玩家 {} 成功将箱子分享给离线玩家 {}.", ownerUuid, offlinePlayerName);
                            } else {
                                p.sendMessage(txt.getMessage("share.add_fail"));
                                logger.error("玩家 {} 分享给离线玩家 {} 失败。", ownerUuid, offlinePlayerName);
                            }
                        } else {
                            p.sendMessage(TextService::getInstance().getMessage(
                                "share.player_not_found",
                                {
                                    {"player", offlinePlayerName}
                            }
                            ));
                            logger.warn("玩家 {} 尝试分享给不存在的玩家 {}.", ownerUuid, offlinePlayerName);
                        }
                    } else {
                        p.sendMessage(TextService::getInstance().getMessage("share.name_empty"));
                    }
                }
            }
            auto& regionRef = p.getDimensionBlockSource();
            showShareForm(p, pos, dimId, ownerUuid, regionRef); // 返回主菜单
        }
    );
}

void showAddShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region,
    int                currentPage
) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage(
        "form.share_add_online_title",
        {
            {"page", std::to_string(currentPage + 1)}
    }
    ));

    // 获取所有在线玩家
    std::vector<std::pair<std::string, std::string>> onlinePlayers; // pair: uuid, name
    auto                                             level = ll::service::getLevel();
    if (level) {
        level->forEachPlayer([&](Player& onlinePlayer) {
            onlinePlayers.push_back({onlinePlayer.getUuid().asString(), onlinePlayer.getRealName()});
            return true;
        });
    }

    // 计算分页
    int totalPlayers = onlinePlayers.size();
    int totalPages   = (totalPlayers + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;

    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex   = std::min(startIndex + ITEMS_PER_PAGE, totalPlayers);

    std::vector<std::string> currentPageUuids;

    if (totalPages > 1) {
        fm.appendLabel(txt.getMessage("form.page_selection"));
        fm.appendSlider("page_slider", txt.getMessage("form.select_page"), 1, totalPages, 1, currentPage + 1);
        fm.appendLabel(txt.getMessage("form.player_selection_tip"));
    }

    if (totalPlayers > 0) {
        fm.appendLabel(txt.getMessage("form.select_online_player"));
        for (int i = startIndex; i < endIndex; ++i) {
            fm.appendToggle(onlinePlayers[i].first, onlinePlayers[i].second, false);
            currentPageUuids.push_back(onlinePlayers[i].first);
        }
    } else {
        fm.appendLabel(txt.getMessage("form.no_online_players"));
    }

    fm.sendTo(
        player,
        [pos, dimId, ownerUuid, currentPageUuids, currentPage, totalPages](
            Player&                           p,
            const ll::form::CustomFormResult& res,
            ll::form::FormCancelReason        reason
        ) {
            auto& region = p.getDimensionBlockSource();
            if (!res) {
                logger.info("玩家 {} 取消了添加分享玩家。", p.getUuid().asString());
                showShareForm(p, pos, dimId, ownerUuid, region, currentPage); // 返回主菜单
                return;
            }

            // 获取滑块值
            int selectedPage = currentPage;
            if (totalPages > 1 && res->count("page_slider")) {
                const auto& sliderResult = res->at("page_slider");
                if (std::holds_alternative<double>(sliderResult)) {
                    selectedPage = static_cast<int>(std::get<double>(sliderResult)) - 1;
                }
            }

            // 如果页码改变，则重新显示当前表单
            if (selectedPage != currentPage) {
                showAddShareForm(p, pos, dimId, ownerUuid, region, selectedPage);
                return;
            }

            // 处理在线玩家开关结果
            auto& svc = ChestService::getInstance();
            auto& txt = TextService::getInstance();
            for (const std::string& onlinePlayerUuid : currentPageUuids) {
                if (res->count(onlinePlayerUuid)) {
                    const auto& toggleResult = res->at(onlinePlayerUuid);
                    if (std::holds_alternative<uint64>(toggleResult) && std::get<uint64>(toggleResult) == 1) {
                        std::string onlinePlayerName = getPlayerNameFromUuid(onlinePlayerUuid);
                        if (svc.addSharedPlayer(ownerUuid, onlinePlayerUuid, pos, dimId, region)) {
                            p.sendMessage(txt.getMessage(
                                "share.add_success",
                                {
                                    {"player", onlinePlayerName}
                            }
                            ));
                            logger.debug("玩家 {} 成功分享给玩家 {}.", ownerUuid, onlinePlayerName);
                        } else {
                            p.sendMessage(txt.getMessage("share.add_fail"));
                            logger.error("玩家 {} 分享给玩家 {} 失败。", ownerUuid, onlinePlayerName);
                        }
                    }
                }
            }

            // 重新显示主表单
            showShareForm(p, pos, dimId, ownerUuid, region, currentPage);
        }
    );
}

void showRemoveShareForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& ownerUuid,
    BlockSource&       region,
    int                currentPage
) {
    ll::form::CustomForm removeForm; // 改为 CustomForm
    auto&                txt = TextService::getInstance();
    removeForm.setTitle(txt.getMessage(
        "form.remove_share_title",
        {
            {"page", std::to_string(currentPage + 1)}
    }
    ));

    std::vector<std::string> sharedPlayers = ChestService::getInstance().getSharedPlayers(pos, dimId, region);

    // 计算分页
    int totalPlayers = sharedPlayers.size();
    int totalPages   = (totalPlayers + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;

    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex   = std::min(startIndex + ITEMS_PER_PAGE, totalPlayers);

    std::vector<std::string> currentPageSharedUuids;

    if (totalPages > 1) {
        removeForm.appendLabel(txt.getMessage("form.page_selection"));
        removeForm.appendSlider("page_slider", txt.getMessage("form.select_page"), 1, totalPages, 1, currentPage + 1);
        removeForm.appendLabel(txt.getMessage("form.player_selection_tip"));
    }

    if (sharedPlayers.empty()) {
        removeForm.appendLabel(txt.getMessage("form.no_shared_players"));
    } else {
        removeForm.appendLabel(txt.getMessage("form.select_remove_player"));
        for (int i = startIndex; i < endIndex; ++i) {
            std::string sharedPlayerUuid = sharedPlayers[i];
            std::string sharedPlayerName = getPlayerNameFromUuid(sharedPlayerUuid);
            removeForm.appendToggle(sharedPlayerUuid, sharedPlayerName, false); // 添加开关
            currentPageSharedUuids.push_back(sharedPlayerUuid);
        }
    }

    removeForm.sendTo(
        player,
        [pos, dimId, ownerUuid, currentPageSharedUuids, currentPage, totalPages](
            Player&                           p,
            const ll::form::CustomFormResult& res,
            ll::form::FormCancelReason        reason
        ) {
            auto& region = p.getDimensionBlockSource();
            if (!res) {
                logger.debug("玩家 {} 取消了移除分享玩家。", p.getUuid().asString());
                showShareForm(p, pos, dimId, ownerUuid, region, currentPage); // 返回主菜单
                return;
            }

            // 获取滑块值
            int selectedPage = currentPage;
            if (totalPages > 1 && res->count("page_slider")) {
                const auto& sliderResult = res->at("page_slider");
                if (std::holds_alternative<double>(sliderResult)) {
                    selectedPage = static_cast<int>(std::get<double>(sliderResult)) - 1;
                }
            }

            // 如果页码改变，则重新显示当前表单
            if (selectedPage != currentPage) {
                showRemoveShareForm(p, pos, dimId, ownerUuid, region, selectedPage);
                return;
            }

            // 处理开关结果
            auto& svc = ChestService::getInstance();
            auto& txt = TextService::getInstance();
            for (const std::string& sharedPlayerUuid : currentPageSharedUuids) {
                if (res->count(sharedPlayerUuid)) {
                    const auto& toggleResult = res->at(sharedPlayerUuid);
                    if (std::holds_alternative<uint64>(toggleResult) && std::get<uint64>(toggleResult) == 1) {
                        std::string sharedPlayerName = getPlayerNameFromUuid(sharedPlayerUuid);
                        if (svc.removeSharedPlayer(sharedPlayerUuid, pos, dimId, region)) {
                            p.sendMessage(txt.getMessage(
                                "share.remove_success",
                                {
                                    {"player", sharedPlayerName}
                            }
                            ));
                            logger.info("玩家 {} 成功移除分享玩家 {}.", ownerUuid, sharedPlayerName);
                        } else {
                            p.sendMessage(txt.getMessage("share.remove_fail"));
                            logger.error("玩家 {} 移除分享玩家 {} 失败。", ownerUuid, sharedPlayerUuid);
                        }
                    }
                }
            }

            // 重新显示移除表单
            showRemoveShareForm(p, pos, dimId, ownerUuid, region, currentPage);
        }
    );
}

} // namespace CT
