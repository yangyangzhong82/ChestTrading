#include "DynamicPricingForm.h"
#include "FormUtils.h"
#include "Utils/MoneyFormat.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "repository/ItemRepository.h"
#include "service/DynamicPricingService.h"
#include "service/I18nService.h"
#include <algorithm>
#include <cctype>

namespace {

std::string trimCopy(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    auto begin   = std::find_if_not(value.begin(), value.end(), isSpace);
    if (begin == value.end()) return "";
    auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(begin, end);
}

bool parseIntStrict(const std::string& raw, int& out) {
    std::string value = trimCopy(raw);
    if (value.empty()) return false;
    size_t pos = 0;
    try {
        out = std::stoi(value, &pos);
    } catch (...) {
        return false;
    }
    return pos == value.size();
}

bool parseDoubleStrict(const std::string& raw, double& out) {
    std::string value = trimCopy(raw);
    if (value.empty()) return false;
    size_t pos = 0;
    try {
        out = std::stod(value, &pos);
    } catch (...) {
        return false;
    }
    return pos == value.size();
}

} // namespace

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

    // 仅用于表单展示：按阈值升序排列，避免打开表单时出现“阶梯倒序”
    std::string           baseTierPrice;
    std::vector<PriceTier> formExtraTiers;
    if (existingOpt) {
        std::vector<PriceTier> sortedTiers = existingOpt->priceTiers;
        std::sort(sortedTiers.begin(), sortedTiers.end(), [](const PriceTier& a, const PriceTier& b) {
            return a.threshold < b.threshold;
        });

        for (const auto& tier : sortedTiers) {
            if (tier.threshold <= 0) {
                if (baseTierPrice.empty()) {
                    baseTierPrice = std::to_string(tier.price);
                }
                continue;
            }
            formExtraTiers.push_back(tier);
        }

        // 兼容异常历史数据：缺少阈值0时，回退到最小阈值档作为基础价展示
        if (baseTierPrice.empty() && !sortedTiers.empty()) {
            baseTierPrice = std::to_string(sortedTiers.front().price);
        }
    }

    for (size_t i = 0; i < 8; ++i) {
        std::string threshold = "", price = "";
        if (existingOpt) {
            if (i == 0) {
                price = baseTierPrice;
            } else {
                size_t extraIndex = i - 1;
                if (extraIndex < formExtraTiers.size()) {
                    threshold = std::to_string(formExtraTiers[extraIndex].threshold);
                    price     = std::to_string(formExtraTiers[extraIndex].price);
                }
            }
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
                    std::string tier1Str = trimCopy(std::get<std::string>(result->at("tier1_price")));
                    double      tier1Price = 100.0;
                    if (!tier1Str.empty() && !parseDoubleStrict(tier1Str, tier1Price)) {
                        p.sendMessage(i18n.get("dynamic_pricing.base_price_invalid"));
                        return;
                    }
                    if (tier1Price <= 0) {
                        p.sendMessage(i18n.get("dynamic_pricing.base_price_positive"));
                        return;
                    }
                    tiers.push_back({0, tier1Price});

                    // 阶梯2-8
                    for (int i = 2; i <= 8; ++i) {
                        std::string idx = std::to_string(i);
                        std::string thresholdStr =
                            trimCopy(std::get<std::string>(result->at("tier" + idx + "_threshold")));
                        std::string priceStr = trimCopy(std::get<std::string>(result->at("tier" + idx + "_price")));
                        bool thresholdFilled = !thresholdStr.empty();
                        bool priceFilled     = !priceStr.empty();

                        if (!thresholdFilled && !priceFilled) {
                            continue;
                        }
                        if (thresholdFilled != priceFilled) {
                            p.sendMessage(i18n.get("dynamic_pricing.tier_pair_required", {{"n", idx}}));
                            return;
                        }

                        int    threshold = 0;
                        double price     = 0.0;
                        if (!parseIntStrict(thresholdStr, threshold)) {
                            p.sendMessage(i18n.get("dynamic_pricing.tier_threshold_integer", {{"n", idx}}));
                            return;
                        }
                        if (!parseDoubleStrict(priceStr, price)) {
                            p.sendMessage(i18n.get("dynamic_pricing.tier_price_invalid", {{"n", idx}}));
                            return;
                        }
                        if (threshold <= 0) {
                            p.sendMessage(i18n.get("dynamic_pricing.tier_threshold_positive", {{"n", idx}}));
                            return;
                        }
                        if (price <= 0) {
                            p.sendMessage(i18n.get("dynamic_pricing.tier_price_positive", {{"n", idx}}));
                            return;
                        }
                        tiers.push_back({threshold, price});
                    }

                    if (tiers.empty()) {
                        p.sendMessage(i18n.get("dynamic_pricing.no_tiers"));
                        return;
                    }

                    std::string stopStr  = trimCopy(std::get<std::string>(result->at("stop_threshold")));
                    std::string resetStr = trimCopy(std::get<std::string>(result->at("reset_hours")));
                    if (stopStr.empty()) {
                        stopThreshold = -1;
                    } else if (!parseIntStrict(stopStr, stopThreshold)) {
                        p.sendMessage(i18n.get("dynamic_pricing.stop_threshold_invalid"));
                        return;
                    }

                    if (resetStr.empty()) {
                        resetHours = 24;
                    } else if (!parseIntStrict(resetStr, resetHours)) {
                        p.sendMessage(i18n.get("dynamic_pricing.reset_hours_invalid"));
                        return;
                    }
                    if (resetHours < 1) resetHours = 1; // 最小1小时
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
            } catch (...) {
                p.sendMessage(i18n.get("dynamic_pricing.form_data_error"));
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
