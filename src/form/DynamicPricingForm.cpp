#include "DynamicPricingForm.h"
#include "FormUtils.h"
#include "Utils/MoneyFormat.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "repository/ItemRepository.h"
#include "service/DynamicPricingService.h"
#include "service/I18nService.h"

namespace CT {

void showDynamicPricingForm(Player& player, BlockPos pos, int dimId, int itemId, bool isShop) {
    auto& i18n = I18nService::getInstance();

    // 获取物品名称
    std::string itemNbt  = ItemRepository::getInstance().getItemNbt(itemId);
    auto        itemPtr  = CT::FormUtils::createItemStackFromNbtString(itemNbt);
    std::string itemName = itemPtr ? std::string(itemPtr->getName()) : "Unknown";

    // 获取现有配置
    auto existingOpt = DynamicPricingService::getInstance().getDynamicPricing(pos, dimId, itemId, isShop);

    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("dynamic_pricing.form_title"));

    fm.appendLabel(i18n.get(
        "dynamic_pricing.item_label",
        {
            {"item", itemName}
    }
    ));

    // 显示当前状态
    if (existingOpt) {
        auto infoOpt = DynamicPricingService::getInstance().getPriceInfo(pos, dimId, itemId, isShop);
        if (infoOpt) {
            fm.appendLabel(i18n.get(
                "dynamic_pricing.current_status",
                {
                    {"count", std::to_string(infoOpt->currentCount)                    },
                    {"limit",
                     infoOpt->stopThreshold > 0 ? std::to_string(infoOpt->stopThreshold)
                                                : i18n.get("dynamic_pricing.unlimited")},
                    {"price", MoneyFormat::format(infoOpt->currentPrice)               },
                    {"reset", std::to_string(infoOpt->hoursUntilReset)                 }
            }
            ));
        }
    }

    // 启用开关
    bool isEnabled = existingOpt ? existingOpt->enabled : true;
    fm.appendToggle("enabled", i18n.get("dynamic_pricing.enabled"), isEnabled);

    // 动态生成8个价格梯度
    const std::vector<std::pair<int, int>> defaultTiers = {
        {0,     100},
        {128,   120},
        {512,   150},
        {2048,  200},
        {4096,  250},
        {8192,  300},
        {16384, 400},
        {32768, 500}
    };

    for (size_t i = 0; i < 8; ++i) {
        std::string threshold = "", price = "";
        if (existingOpt && existingOpt->priceTiers.size() > i) {
            if (i > 0) threshold = std::to_string(existingOpt->priceTiers[i].threshold);
            price = std::to_string(existingOpt->priceTiers[i].price);
        }

        std::string idx = std::to_string(i + 1);
        if (i == 0) {
            fm.appendInput(
                "tier1_price",
                i18n.get("dynamic_pricing.tier1_price"),
                std::to_string(defaultTiers[0].second),
                price
            );
        } else {
            fm.appendInput(
                "tier" + idx + "_threshold",
                i18n.get(
                    "dynamic_pricing.tier_threshold",
                    {
                        {"n", idx}
            }
                ),
                std::to_string(defaultTiers[i].first),
                threshold
            );
            fm.appendInput(
                "tier" + idx + "_price",
                i18n.get(
                    "dynamic_pricing.tier_price",
                    {
                        {"n", idx}
            }
                ),
                std::to_string(defaultTiers[i].second),
                price
            );
        }
    }

    // 停止阈值
    std::string stopThreshold = existingOpt ? std::to_string(existingOpt->stopThreshold) : "-1";
    fm.appendInput("stop_threshold", i18n.get("dynamic_pricing.stop_threshold"), "4096", stopThreshold);
    fm.appendLabel(i18n.get("dynamic_pricing.stop_threshold_hint"));

    // 重置周期
    std::string resetHours = existingOpt ? std::to_string(existingOpt->resetIntervalHours) : "24";
    fm.appendInput("reset_hours", i18n.get("dynamic_pricing.reset_hours"), "24", resetHours);

    fm.sendTo(
        player,
        [pos, dimId, itemId, isShop](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& i18n = I18nService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(i18n.get("action.cancelled"));
                return;
            }

            try {
                bool enabled = std::get<uint64_t>(result->at("enabled")) != 0;

                std::vector<PriceTier> tiers;
                int                    stopThreshold = -1;
                int                    resetHours    = 24;

                if (enabled) {
                    // 阶梯1（基础价格）
                    std::string tier1Str   = std::get<std::string>(result->at("tier1_price"));
                    double      tier1Price = tier1Str.empty() ? 100.0 : std::stod(tier1Str);
                    if (tier1Price > 0) {
                        tiers.push_back({0, tier1Price});
                    }

                    // 阶梯2-8
                    for (int i = 2; i <= 8; ++i) {
                        std::string idx          = std::to_string(i);
                        std::string thresholdStr = std::get<std::string>(result->at("tier" + idx + "_threshold"));
                        std::string priceStr     = std::get<std::string>(result->at("tier" + idx + "_price"));
                        if (!thresholdStr.empty() && !priceStr.empty()) {
                            int    threshold = std::stoi(thresholdStr);
                            double price     = std::stod(priceStr);
                            if (threshold > 0 && price > 0) {
                                tiers.push_back({threshold, price});
                            }
                        }
                    }

                    if (tiers.empty()) {
                        p.sendMessage(i18n.get("dynamic_pricing.no_tiers"));
                        return;
                    }

                    std::string stopStr  = std::get<std::string>(result->at("stop_threshold"));
                    std::string resetStr = std::get<std::string>(result->at("reset_hours"));
                    stopThreshold        = stopStr.empty() ? -1 : std::stoi(stopStr);
                    resetHours           = resetStr.empty() ? 24 : std::stoi(resetStr);
                    if (resetHours <= 0) resetHours = 24;
                } else {
                    // 禁用时使用默认值
                    tiers.push_back({0, 100});
                }

                if (DynamicPricingService::getInstance()
                        .setDynamicPricing(pos, dimId, itemId, isShop, tiers, stopThreshold, resetHours, enabled)) {
                    p.sendMessage(i18n.get("dynamic_pricing.set_success"));
                } else {
                    p.sendMessage(i18n.get("dynamic_pricing.set_fail"));
                }
            } catch (const std::exception& e) {
                p.sendMessage(i18n.get("input.invalid_number"));
            }
        }
    );
}

void showDynamicPricingListForm(Player& player, BlockPos pos, int dimId, bool isShop) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("dynamic_pricing.list_title"));

    auto allPricing = DynamicPricingRepository::getInstance().findAll(pos, dimId);

    // 筛选商店或回收
    std::vector<DynamicPricingData> filtered;
    for (const auto& dp : allPricing) {
        if (dp.isShop == isShop) {
            filtered.push_back(dp);
        }
    }

    if (filtered.empty()) {
        fm.setContent(i18n.get("dynamic_pricing.no_items"));
    } else {
        fm.setContent(i18n.get("dynamic_pricing.list_content"));

        for (const auto& dp : filtered) {
            std::string itemNbt  = ItemRepository::getInstance().getItemNbt(dp.itemId);
            auto        itemPtr  = CT::FormUtils::createItemStackFromNbtString(itemNbt);
            std::string itemName = itemPtr ? std::string(itemPtr->getName()) : "Unknown";

            auto infoOpt = DynamicPricingService::getInstance().getPriceInfo(pos, dimId, dp.itemId, isShop);

            std::string buttonText = itemName + "\n";
            if (infoOpt) {
                buttonText += i18n.get(
                    "dynamic_pricing.item_status",
                    {
                        {"count", std::to_string(infoOpt->currentCount)                    },
                        {"limit",
                         infoOpt->stopThreshold > 0 ? std::to_string(infoOpt->stopThreshold)
                                                    : i18n.get("dynamic_pricing.unlimited")},
                        {"price", MoneyFormat::format(infoOpt->currentPrice)               }
                }
                );
            }

            int capturedItemId = dp.itemId;
            fm.appendButton(buttonText, [pos, dimId, capturedItemId, isShop](Player& p) {
                showDynamicPricingForm(p, pos, dimId, capturedItemId, isShop);
            });
        }
    }

    fm.appendButton(i18n.get("form.button_back"), [](Player&) {});
    fm.sendTo(player);
}

} // namespace CT
