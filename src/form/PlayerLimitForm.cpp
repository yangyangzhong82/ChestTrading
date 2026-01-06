#include "PlayerLimitForm.h"
#include "FormUtils.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "service/I18nService.h"
#include "service/PlayerLimitService.h"

namespace CT {

void showPlayerLimitForm(Player& player, BlockPos pos, int dimId, BlockSource& region, bool isShop) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get(isShop ? "limit.shop_title" : "limit.recycle_title"));

    // 获取当前全局限制（playerUuid为空表示全局限制）
    auto existingLimit = PlayerLimitService::getInstance().getLimit(pos, dimId, "", isShop);

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
        [pos, dimId, isShop](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& i18n = I18nService::getInstance();

            if (!result.has_value()) {
                p.sendMessage(i18n.get("action.cancelled"));
                return;
            }

            try {
                int limitCount   = std::stoi(std::get<std::string>(result->at("limit_count")));
                int limitSeconds = std::stoi(std::get<std::string>(result->at("limit_seconds")));

                if (limitCount <= 0) {
                    // 删除限制
                    PlayerLimitService::getInstance().removeLimit(pos, dimId, "", isShop);
                    p.sendMessage(i18n.get("limit.removed"));
                } else {
                    PlayerLimitService::getInstance().setLimit(pos, dimId, "", limitCount, limitSeconds, isShop);
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

} // namespace CT
