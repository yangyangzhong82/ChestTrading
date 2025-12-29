#include "AdminForm.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "repository/ChestRepository.h"
#include "service/I18nService.h"
#include "service/TextService.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>


namespace CT {

std::string chestTypeToString(ChestType type) {
    auto& txt = TextService::getInstance();
    switch (type) {
    case ChestType::Locked:
        return txt.getChestTypeName(ChestType::Locked);
    case ChestType::RecycleShop:
        return txt.getChestTypeName(ChestType::RecycleShop);
    case ChestType::Shop:
        return txt.getChestTypeName(ChestType::Shop);
    case ChestType::Public:
        return txt.getChestTypeName(ChestType::Public);
    default:
        return txt.getChestTypeName(ChestType::Locked);
    }
}

std::string dimIdToString(int dimId) {
    auto& i18n = I18nService::getInstance();
    switch (dimId) {
    case 0:
        return i18n.get("dimension.overworld");
    case 1:
        return i18n.get("dimension.nether");
    case 2:
        return i18n.get("dimension.end");
    default:
        return i18n.get("dimension.unknown");
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
    auto&                i18n = I18nService::getInstance();
    auto&                txt  = TextService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("admin.filter_title"));

    fm.appendHeader(i18n.get("admin.filter_by_dimension"));
    fm.appendToggle("dim_all", i18n.get("admin.all_dimensions"), true);
    fm.appendToggle("dim_0", i18n.get("dimension.overworld"), false);
    fm.appendToggle("dim_1", i18n.get("dimension.nether"), false);
    fm.appendToggle("dim_2", i18n.get("dimension.end"), false);
    fm.appendDivider();

    fm.appendHeader(i18n.get("admin.filter_by_type"));
    fm.appendToggle("type_all", i18n.get("admin.all_types"), true);
    fm.appendToggle("type_1", txt.getChestTypeName(ChestType::Locked), false);
    fm.appendToggle("type_2", txt.getChestTypeName(ChestType::RecycleShop), false);
    fm.appendToggle("type_3", txt.getChestTypeName(ChestType::Shop), false);
    fm.appendToggle("type_4", txt.getChestTypeName(ChestType::Public), false);
    fm.appendDivider();

    fm.appendHeader(i18n.get("admin.filter_by_coords"));
    fm.appendInput("pos1", i18n.get("admin.start_coords"), i18n.get("admin.coords_example"));
    fm.appendInput("pos2", i18n.get("admin.end_coords"), i18n.get("admin.coords_example"));

    fm.sendTo(player, [](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
        if (!result.has_value() || result->empty()) {
            auto& i18n = I18nService::getInstance();
            p.sendMessage(i18n.get("admin.form_closed"));
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
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("admin.manage_title"));

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
        i18n.get("admin.direct_teleport"),
        false,
        i18n.get("admin.direct_teleport_hint")
    );

    if (filteredChests.empty()) {
        fm.appendLabel(i18n.get("admin.no_chests_found"));
    } else {
        if (totalPages > 1) {
            fm.appendSlider(
                "page_slider",
                i18n.get("admin.page_slider"),
                1,
                totalPages,
                1,
                currentPage,
                i18n.get(
                    "admin.page_slider_hint",
                    {
                        {"current", std::to_string(currentPage)},
                        {"total",   std::to_string(totalPages) }
            }
                )
            );
        }

        std::map<std::string, std::vector<ChestData>> playerChests;
        for (const auto& chest : filteredChests) {
            playerChests[chest.ownerUuid].push_back(chest);
        }

        fm.appendHeader(i18n.get(
            "admin.chest_list_header",
            {
                {"total",       std::to_string(filteredChests.size())},
                {"current",     std::to_string(currentPage)          },
                {"total_pages", std::to_string(totalPages)           }
        }
        ));
        fm.appendDivider();

        int startIndex = (currentPage - 1) * itemsPerPage;
        int endIndex   = std::min(startIndex + itemsPerPage, (int)filteredChests.size());

        std::vector<ChestData> pagedChests;
        for (const auto& pair : playerChests) {
            pagedChests.insert(pagedChests.end(), pair.second.begin(), pair.second.end());
        }

        // 预先批量查询当前页所有玩家名称，避免 N+1 查询
        std::map<std::string, std::string> ownerNameCache;
        for (int i = startIndex; i < endIndex; ++i) {
            const auto& uuid = pagedChests[i].ownerUuid;
            if (ownerNameCache.find(uuid) == ownerNameCache.end()) {
                auto playerInfo      = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid));
                ownerNameCache[uuid] = playerInfo ? playerInfo->name : uuid;
            }
        }

        for (int i = startIndex; i < endIndex; ++i) {
            const auto&        chest     = pagedChests[i];
            const std::string& ownerName = ownerNameCache[chest.ownerUuid];

            std::string label = "§b" + ownerName + " §f- " + dimIdToString(chest.dimId) + " §7- "
                              + chestTypeToString(chest.type) + " §r§e[" + std::to_string(chest.pos.x) + ", "
                              + std::to_string(chest.pos.y) + ", " + std::to_string(chest.pos.z) + "]";

            std::string toggleName = chest.ownerUuid + "|" + std::to_string(chest.dimId) + "|"
                                   + std::to_string(chest.pos.x) + "|" + std::to_string(chest.pos.y) + "|"
                                   + std::to_string(chest.pos.z);

            fm.appendToggle(toggleName, label, false, i18n.get("admin.select_chest_hint"));
        }
        fm.appendDivider();
    }

    fm.sendTo(
        player,
        [dimIdFilter,
         chestTypeFilter,
         coordRange](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            auto& i18n = I18nService::getInstance();
            if (!result.has_value() || result->empty()) {
                p.sendMessage(i18n.get("admin.manage_closed"));
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
                        p.sendMessage(i18n.get("admin.teleport_failed"));
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
