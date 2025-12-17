#include "AdminForm.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "repository/ChestRepository.h"
#include "service/TextService.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>


namespace CT {

std::string chestTypeToString(ChestType type) {
    switch (type) {
    case ChestType::Locked:
        return "上锁箱";
    case ChestType::RecycleShop:
        return "回收商店";
    case ChestType::Shop:
        return "商店";
    case ChestType::Public:
        return "公共箱子";
    default:
        return "未知";
    }
}

std::string dimIdToString(int dimId) {
    switch (dimId) {
    case 0:
        return "主世界";
    case 1:
        return "下界";
    case 2:
        return "末地";
    default:
        return "未知维度";
    }
}

// 解析 "x,y,z" 格式的字符串
std::optional<BlockPos> parseCoordinates(const std::string& coordStr) {
    if (coordStr.empty()) {
        return std::nullopt;
    }
    std::stringstream ss(coordStr);
    std::string       item;
    std::vector<int>  coords;
    while (std::getline(ss, item, ',')) {
        try {
            coords.push_back(std::stoi(item));
        } catch (const std::exception&) {
            return std::nullopt; // 解析失败
        }
    }
    if (coords.size() == 3) {
        return BlockPos{coords[0], coords[1], coords[2]};
    }
    return std::nullopt;
}


void showAdminMainForm(Player& player) {
    ll::form::CustomForm fm;
    fm.setTitle("箱子管理筛选");

    fm.appendHeader("§l按维度筛选（可多选）");
    fm.appendToggle("dim_all", "所有维度", true);
    fm.appendToggle("dim_0", "主世界", false);
    fm.appendToggle("dim_1", "下界", false);
    fm.appendToggle("dim_2", "末地", false);
    fm.appendDivider();

    fm.appendHeader("§l按箱子类型筛选（可多选）");
    fm.appendToggle("type_all", "所有类型", true);
    fm.appendToggle("type_1", "上锁箱", false);
    fm.appendToggle("type_2", "回收商店", false);
    fm.appendToggle("type_3", "商店", false);
    fm.appendToggle("type_4", "公共箱子", false);
    fm.appendDivider();

    fm.appendHeader("§l按坐标范围筛选（留空则不限制）");
    fm.appendInput("pos1", "起始坐标 (x,y,z)", "例如: 100,60,100");
    fm.appendInput("pos2", "结束坐标 (x,y,z)", "例如: 200,50,200");

    fm.sendTo(player, [](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
        if (!result.has_value() || result->empty()) {
            p.sendMessage("§c你关闭了筛选表单。");
            return;
        }

        std::vector<int> dimIdFilter;
        if (!std::get<uint64>(result->at("dim_all"))) {
            if (std::get<uint64>(result->at("dim_0"))) dimIdFilter.push_back(0);
            if (std::get<uint64>(result->at("dim_1"))) dimIdFilter.push_back(1);
            if (std::get<uint64>(result->at("dim_2"))) dimIdFilter.push_back(2);
        }

        std::vector<ChestType> chestTypeFilter;
        if (!std::get<uint64>(result->at("type_all"))) {
            if (std::get<uint64>(result->at("type_1"))) chestTypeFilter.push_back(ChestType::Locked);
            if (std::get<uint64>(result->at("type_2"))) chestTypeFilter.push_back(ChestType::RecycleShop);
            if (std::get<uint64>(result->at("type_3"))) chestTypeFilter.push_back(ChestType::Shop);
            if (std::get<uint64>(result->at("type_4"))) chestTypeFilter.push_back(ChestType::Public);
        }

        CoordinateRange coordRange;
        auto            pos1Str = std::get<std::string>(result->at("pos1"));
        auto            pos2Str = std::get<std::string>(result->at("pos2"));
        auto            pos1Opt = parseCoordinates(pos1Str);
        auto            pos2Opt = parseCoordinates(pos2Str);

        if (pos1Opt && pos2Opt) {
            coordRange.minX = std::min(pos1Opt->x, pos2Opt->x);
            coordRange.maxX = std::max(pos1Opt->x, pos2Opt->x);
            coordRange.minY = std::min(pos1Opt->y, pos2Opt->y);
            coordRange.maxY = std::max(pos1Opt->y, pos2Opt->y);
            coordRange.minZ = std::min(pos1Opt->z, pos2Opt->z);
            coordRange.maxZ = std::max(pos1Opt->z, pos2Opt->z);
        } else if (pos1Opt && !pos2Opt) {
            // 如果只填了第一个坐标，则进行单点筛选
            coordRange.minX = coordRange.maxX = pos1Opt->x;
            coordRange.minY = coordRange.maxY = pos1Opt->y;
            coordRange.minZ = coordRange.maxZ = pos1Opt->z;
        }

        showAdminForm(p, 1, dimIdFilter, chestTypeFilter, coordRange);
    });
}


void showAdminForm(
    Player&                       player,
    int                           currentPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange
) {
    ll::form::CustomForm fm;
    fm.setTitle("服务器箱子管理");

    auto                   allChests = ChestRepository::getInstance().findAll();
    std::vector<ChestData> filteredChests;

    // 应用筛选
    std::copy_if(allChests.begin(), allChests.end(), std::back_inserter(filteredChests), [&](const ChestData& chest) {
        bool dimMatch =
            dimIdFilter.empty() || std::find(dimIdFilter.begin(), dimIdFilter.end(), chest.dimId) != dimIdFilter.end();
        bool typeMatch = chestTypeFilter.empty()
                      || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), chest.type) != chestTypeFilter.end();

        bool coordMatch = true;
        if (coordRange.minX.has_value()) { // 只要有一个坐标值，就进行判断
            coordMatch = (!coordRange.minX.has_value() || chest.pos.x >= *coordRange.minX)
                      && (!coordRange.maxX.has_value() || chest.pos.x <= *coordRange.maxX)
                      && (!coordRange.minY.has_value() || chest.pos.y >= *coordRange.minY)
                      && (!coordRange.maxY.has_value() || chest.pos.y <= *coordRange.maxY)
                      && (!coordRange.minZ.has_value() || chest.pos.z >= *coordRange.minZ)
                      && (!coordRange.maxZ.has_value() || chest.pos.z <= *coordRange.maxZ);
        }

        return dimMatch && typeMatch && coordMatch;
    });

    const int itemsPerPage = 10;
    const int totalPages   = (filteredChests.empty()) ? 1 : (filteredChests.size() + itemsPerPage - 1) / itemsPerPage;
    currentPage            = std::max(1, std::min(currentPage, totalPages));

    fm.appendToggle(
        "confirm_teleport",
        "直接传送（开启后选择箱子将直接传送）",
        false,
        "§7开启后，点击箱子开关将立即传送，而不是翻页。"
    );

    if (filteredChests.empty()) {
        fm.appendLabel("没有找到符合筛选条件的箱子。");
    } else {
        if (totalPages > 1) {
            fm.appendSlider(
                "page_slider",
                "翻页",
                1,
                totalPages,
                1,
                currentPage,
                "§7拖动滑块选择页面 (当前: " + std::to_string(currentPage) + "/" + std::to_string(totalPages) + ")"
            );
        }

        std::map<std::string, std::vector<ChestData>> playerChests;
        for (const auto& chest : filteredChests) {
            playerChests[chest.ownerUuid].push_back(chest);
        }

        fm.appendHeader(
            "§l§e箱子列表 §r§7(总计: " + std::to_string(filteredChests.size()) + " | 第 " + std::to_string(currentPage)
            + " / " + std::to_string(totalPages) + " 页)"
        );
        fm.appendDivider();

        int startIndex = (currentPage - 1) * itemsPerPage;
        int endIndex   = std::min(startIndex + itemsPerPage, (int)filteredChests.size());

        std::vector<ChestData> pagedChests;
        for (const auto& pair : playerChests) {
            pagedChests.insert(pagedChests.end(), pair.second.begin(), pair.second.end());
        }

        for (int i = startIndex; i < endIndex; ++i) {
            const auto& chest = pagedChests[i];

            auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(chest.ownerUuid));
            std::string ownerName = playerInfo ? playerInfo->name : chest.ownerUuid;

            std::string label = "§b" + ownerName + " §f- " + dimIdToString(chest.dimId) + " §7- "
                              + chestTypeToString(chest.type) + " §r§e[" + std::to_string(chest.pos.x) + ", "
                              + std::to_string(chest.pos.y) + ", " + std::to_string(chest.pos.z) + "]";

            std::string toggleName = chest.ownerUuid + "|" + std::to_string(chest.dimId) + "|"
                                   + std::to_string(chest.pos.x) + "|" + std::to_string(chest.pos.y) + "|"
                                   + std::to_string(chest.pos.z);

            fm.appendToggle(toggleName, label, false, "§7选择此箱子进行操作");
        }
        fm.appendDivider();
    }

    fm.sendTo(
        player,
        [dimIdFilter,
         chestTypeFilter,
         coordRange](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            if (!result.has_value() || result->empty()) {
                p.sendMessage("§c你关闭了管理表单。");
                return;
            }

            bool confirmTeleport = false;
            if (result->count("confirm_teleport") && std::get<uint64>(result->at("confirm_teleport")) == 1) {
                confirmTeleport = true;
            }

            std::string selectedChestToggle;
            for (const auto& [toggleName, value] : *result) {
                if (toggleName.rfind("dim_", 0) != 0 && toggleName.rfind("type_", 0) != 0
                    && toggleName != "confirm_teleport" && toggleName != "page_slider" && toggleName != "pos1"
                    && toggleName != "pos2") {
                    if (value.index() == 1 && std::get<uint64>(value) == 1) {
                        selectedChestToggle = toggleName;
                        break;
                    }
                }
            }

            if (confirmTeleport && !selectedChestToggle.empty()) {
                std::stringstream        ss(selectedChestToggle);
                std::string              segment;
                std::vector<std::string> parts;
                while (std::getline(ss, segment, '|')) {
                    parts.push_back(segment);
                }

                if (parts.size() == 5) {
                    try {
                        auto& txt = TextService::getInstance();

                        int dimId = std::stoi(parts[1]);
                        int x     = std::stoi(parts[2]);
                        int y     = std::stoi(parts[3]);
                        int z     = std::stoi(parts[4]);

                        // 管理员传送不受任何限制（不收费、不冷却）
                        p.teleport({x, y, z}, dimId);

                        p.sendMessage(txt.getMessage(
                            "teleport.admin_success",
                            {
                                {"x", std::to_string(x)},
                                {"y", std::to_string(y)},
                                {"z", std::to_string(z)}
                        }
                        ));
                        return;
                    } catch (const std::exception& e) {
                        p.sendMessage("§c传送失败，坐标解析错误。");
                        return;
                    }
                }
            }

            if (result->count("page_slider")) {
                int newPage = static_cast<int>(std::get<double>(result->at("page_slider")));
                showAdminForm(p, newPage, dimIdFilter, chestTypeFilter, coordRange);
            } else {
                showAdminForm(p, 1, dimIdFilter, chestTypeFilter, coordRange);
            }
        }
    );
}

} // namespace CT
