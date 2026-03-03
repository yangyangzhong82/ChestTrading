#include "SalesRankingForm.h"
#include "Config/ConfigManager.h"
#include "FormUtils.h"
#include "Utils/MoneyFormat.h"
#include "ll/api/form/SimpleForm.h"
#include "repository/ShopRepository.h"
#include "service/I18nService.h"
#include <algorithm>

namespace CT {

void showSalesRankingForm(Player& player, int currentPage) {
    auto& i18n   = I18nService::getInstance();
    auto& config = ConfigManager::getInstance().get();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("sales_ranking.title"));

    auto rankings = ShopRepository::getInstance().getPlayerSalesRanking(config.salesRankingSettings.maxDisplayCount);

    if (rankings.empty()) {
        fm.setContent(i18n.get("sales_ranking.no_data"));
    } else {
        std::vector<std::string> uuids;
        for (const auto& data : rankings) {
            uuids.push_back(data.ownerUuid);
        }
        auto nameCache = CT::FormUtils::getPlayerNameCache(uuids);

        std::string content = i18n.get(
            "sales_ranking.header",
            {
                {"count", std::to_string(rankings.size())}
        }
        );
        content += "\n\n";

        for (size_t i = 0; i < rankings.size(); ++i) {
            const auto&        data      = rankings[i];
            const std::string& ownerName = nameCache[data.ownerUuid];
            int                rank      = static_cast<int>(i + 1);

            std::string lastTime = data.lastSaleTime.empty() ? i18n.get("sales_ranking.no_sales") : data.lastSaleTime;
            std::string location = data.lastSaleTime.empty()
                                       ? i18n.get("sales_ranking.no_sales")
                                       : (CT::FormUtils::dimIdToString(data.lastSaleDimId) + " ["
                                          + std::to_string(data.lastSalePos.x) + "," + std::to_string(data.lastSalePos.y)
                                          + "," + std::to_string(data.lastSalePos.z) + "]");

            content += i18n.get(
                "sales_ranking.entry",
                {
                    {"rank",      std::to_string(rank)                      },
                    {"owner",     ownerName                                 },
                    {"location",  location                                  },
                    {"count",     std::to_string(data.totalSalesCount)      },
                    {"revenue",   CT::MoneyFormat::format(data.totalRevenue)},
                    {"last_time", lastTime                                  },
                    {"last_count", std::to_string(data.lastTradeCount)      },
                    {"last_price", CT::MoneyFormat::format(data.lastTradePrice)}
            }
            );
            content += "\n";
        }

        fm.setContent(content);
    }

    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

} // namespace CT
