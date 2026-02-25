#include "PlayerLimitForm.h"
#include "ll/api/form/CustomForm.h"
#include "service/I18nService.h"
#include "service/PlayerLimitService.h"

namespace CT {

namespace {

void showLimitForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    bool               isShop,
    int                itemId,
    const std::string& itemName
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;

    const int normalizedItemId = itemId > 0 ? itemId : 0;
    if (normalizedItemId > 0) {
        std::string displayName = itemName.empty() ? i18n.get("shop.unknown_item") : itemName;
        fm.setTitle(i18n.get(isShop ? "limit.shop_item_title" : "limit.recycle_item_title"));
        fm.appendLabel(i18n.get(
            "limit.scope_item",
            {{"item", displayName}, {"item_id", std::to_string(normalizedItemId)}}
        ));
    } else {
        fm.setTitle(i18n.get(isShop ? "limit.shop_title" : "limit.recycle_title"));
        fm.appendLabel(i18n.get("limit.scope_global"));
    }

    auto existingLimit = PlayerLimitService::getInstance().getLimit(pos, dimId, "", isShop, normalizedItemId);

    fm.appendInput(
        "limit_count",
        i18n.get("limit.input_count"),
        "0",
        existingLimit ? std::to_string(existingLimit->limitCount) : ""
    );
    fm.appendInput(
        "limit_seconds",
        i18n.get("limit.input_seconds"),
        "3600",
        existingLimit ? std::to_string(existingLimit->limitSeconds) : ""
    );
    fm.appendLabel(i18n.get("limit.hint_zero"));

    fm.sendTo(
        player,
        [pos, dimId, isShop, normalizedItemId](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) {
            auto& i18n = I18nService::getInstance();

            if (!result.has_value()) {
                p.sendMessage(i18n.get("action.cancelled"));
                return;
            }

            try {
                int limitCount   = std::stoi(std::get<std::string>(result->at("limit_count")));
                int limitSeconds = std::stoi(std::get<std::string>(result->at("limit_seconds")));

                if (limitCount <= 0) {
                    PlayerLimitService::getInstance().removeLimit(pos, dimId, "", isShop, normalizedItemId);
                    p.sendMessage(i18n.get("limit.removed"));
                } else {
                    PlayerLimitService::getInstance()
                        .setLimit(pos, dimId, "", limitCount, limitSeconds, isShop, normalizedItemId);
                    p.sendMessage(i18n.get(
                        "limit.set_success",
                        {
                            {"count",   std::to_string(limitCount)  },
                            {"seconds", std::to_string(limitSeconds)}
                        }
                    ));
                }
            } catch (...) {
                p.sendMessage(i18n.get("input.invalid_number"));
            }
        }
    );
}

} // namespace

void showPlayerLimitForm(Player& player, BlockPos pos, int dimId, BlockSource& region, bool isShop) {
    (void)region;
    showLimitForm(player, pos, dimId, isShop, 0, "");
}

void showPlayerItemLimitForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    bool               isShop,
    int                itemId,
    const std::string& itemName
) {
    (void)region;
    showLimitForm(player, pos, dimId, isShop, itemId, itemName);
}

} // namespace CT
