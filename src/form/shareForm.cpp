#include "shareForm.h"
#include "interaction/chestprotect.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
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
    fm.setTitle("箱子分享管理");

    std::vector<std::string> sharedPlayers = getSharedPlayers(pos, dimId, region);
    std::string              ownerName     = getPlayerNameFromUuid(ownerUuid);

    std::string content = "§e箱子主人: " + ownerName + "§r\n\n当前已分享的玩家：\n";
    if (sharedPlayers.empty()) {
        content += "无\n";
    } else {
        for (const std::string& sharedPlayerUuid : sharedPlayers) {
            content += "- " + getPlayerNameFromUuid(sharedPlayerUuid) + "\n";
        }
    }
    fm.setContent(content);

    fm.appendButton("添加在线玩家", [pos, dimId, ownerUuid, currentPage](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddShareForm(p, pos, dimId, ownerUuid, region, currentPage);
    });

    fm.appendButton("添加离线玩家", [pos, dimId, ownerUuid](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddOfflineShareForm(p, pos, dimId, ownerUuid, region);
    });

    if (!sharedPlayers.empty()) {
        fm.appendButton("移除分享玩家", [pos, dimId, ownerUuid, currentPage](Player& p) {
            auto& region = p.getDimensionBlockSource();
            showRemoveShareForm(p, pos, dimId, ownerUuid, region, currentPage);
        });
    }

    fm.appendButton("取消", [](Player& p) { logger.info("玩家 {} 取消了箱子分享管理。", p.getUuid().asString()); });

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
    fm.setTitle("添加离线玩家");
    fm.appendInput(OFFLINE_PLAYER_INPUT_KEY, "输入离线玩家名称", "");

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
                            if (addSharedPlayer(ownerUuid, offlinePlayerUuid, pos, dimId, &region)) {
                                p.sendMessage("§a成功添加离线玩家 " + offlinePlayerName + " 到分享列表！");
                                logger.info(
                                    "玩家 {} 成功将箱子 ({}, {}, {}) in dim {} 分享给离线玩家 {} ({}).",
                                    ownerUuid,
                                    pos.x,
                                    pos.y,
                                    pos.z,
                                    dimId,
                                    offlinePlayerName,
                                    offlinePlayerUuid
                                );
                            } else {
                                p.sendMessage("§c添加离线玩家 " + offlinePlayerName + " 失败！");
                                logger.error(
                                    "玩家 {} 尝试将箱子 ({}, {}, {}) in dim {} 分享给离线玩家 {} ({}) 失败。",
                                    ownerUuid,
                                    pos.x,
                                    pos.y,
                                    pos.z,
                                    dimId,
                                    offlinePlayerName,
                                    offlinePlayerUuid
                                );
                            }
                        } else {
                            p.sendMessage("§c未找到玩家 " + offlinePlayerName + "！");
                            logger.warn("玩家 {} 尝试分享给不存在的玩家 {}.", ownerUuid, offlinePlayerName);
                        }
                    } else {
                        p.sendMessage("§c玩家名称不能为空！");
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
    fm.setTitle("添加在线玩家 (第 " + std::to_string(currentPage + 1) + " 页)");

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
        fm.appendLabel("--- 分页选择 ---");
        fm.appendSlider("page_slider", "选择页码", 1, totalPages, 1, currentPage + 1);
        fm.appendLabel("--- 玩家选择 (请在选择页码后提交表单) ---");
    }

    if (totalPlayers > 0) {
        fm.appendLabel("选择要分享的在线玩家：");
        for (int i = startIndex; i < endIndex; ++i) {
            fm.appendToggle(onlinePlayers[i].first, onlinePlayers[i].second, false);
            currentPageUuids.push_back(onlinePlayers[i].first);
        }
    } else {
        fm.appendLabel("当前没有其他在线玩家可供分享。");
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
            bool playersAdded = false;
            for (const std::string& onlinePlayerUuid : currentPageUuids) {
                if (res->count(onlinePlayerUuid)) {
                    const auto& toggleResult = res->at(onlinePlayerUuid);
                    if (std::holds_alternative<uint64>(toggleResult) && std::get<uint64>(toggleResult) == 1) {
                        std::string onlinePlayerName = getPlayerNameFromUuid(onlinePlayerUuid);
                        // 添加分享玩家
                        if (addSharedPlayer(ownerUuid, onlinePlayerUuid, pos, dimId, &region)) {
                            p.sendMessage("§a成功添加玩家 " + onlinePlayerName + " 到分享列表！");
                            logger.debug(
                                "玩家 {} 成功将箱子 ({}, {}, {}) in dim {} 分享给玩家 {} ({}).",
                                ownerUuid,
                                pos.x,
                                pos.y,
                                pos.z,
                                dimId,
                                onlinePlayerName,
                                onlinePlayerUuid
                            );
                            playersAdded = true;
                        } else {
                            p.sendMessage("§c添加分享玩家失败！");
                            logger.error(
                                "玩家 {} 尝试将箱子 ({}, {}, {}) in dim {} 分享给玩家 {} ({}) 失败。",
                                ownerUuid,
                                pos.x,
                                pos.y,
                                pos.z,
                                dimId,
                                onlinePlayerName,
                                onlinePlayerUuid
                            );
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
    removeForm.setTitle("移除分享玩家 (第 " + std::to_string(currentPage + 1) + " 页)");

    std::vector<std::string> sharedPlayers = getSharedPlayers(pos, dimId, region);

    // 计算分页
    int totalPlayers = sharedPlayers.size();
    int totalPages   = (totalPlayers + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;

    int startIndex = currentPage * ITEMS_PER_PAGE;
    int endIndex   = std::min(startIndex + ITEMS_PER_PAGE, totalPlayers);

    std::vector<std::string> currentPageSharedUuids;

    if (totalPages > 1) {
        removeForm.appendLabel("--- 分页选择 ---");
        removeForm.appendSlider("page_slider", "选择页码", 1, totalPages, 1, currentPage + 1);
        removeForm.appendLabel("--- 玩家选择 (请在选择页码后提交表单) ---");
    }

    if (sharedPlayers.empty()) {
        removeForm.appendLabel("当前没有已分享的玩家可供移除。");
    } else {
        removeForm.appendLabel("选择要移除的分享玩家：");
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
            for (const std::string& sharedPlayerUuid : currentPageSharedUuids) {
                if (res->count(sharedPlayerUuid)) {
                    const auto& toggleResult = res->at(sharedPlayerUuid);
                    if (std::holds_alternative<uint64>(toggleResult) && std::get<uint64>(toggleResult) == 1) {
                        std::string sharedPlayerName = getPlayerNameFromUuid(sharedPlayerUuid);
                        if (removeSharedPlayer(sharedPlayerUuid, pos, dimId, &region)) {
                            p.sendMessage("§a成功从分享列表移除玩家 " + sharedPlayerName + "！");
                            logger.info(
                                "玩家 {} 成功从箱子 ({}, {}, {}) in dim {} 移除分享玩家 {} ({}).",
                                ownerUuid,
                                pos.x,
                                pos.y,
                                pos.z,
                                dimId,
                                sharedPlayerName,
                                sharedPlayerUuid
                            );
                        } else {
                            p.sendMessage("§c移除分享玩家失败！");
                            logger.error(
                                "玩家 {} 尝试从箱子 ({}, {}, {}) in dim {} 移除分享玩家 {} 失败。",
                                ownerUuid,
                                pos.x,
                                pos.y,
                                pos.z,
                                dimId,
                                sharedPlayerUuid
                            );
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
