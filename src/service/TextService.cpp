#include "TextService.h"
#include "I18nService.h"
#include "Utils/MoneyFormat.h"

namespace CT {

TextService& TextService::getInstance() {
    static TextService instance;
    return instance;
}

std::string TextService::getMessage(const std::string& key, const std::map<std::string, std::string>& params) {
    return I18nService::getInstance().get(key, params);
}

std::string TextService::generateChestText(ChestType type, const std::string& ownerName) {
    auto& i18n = I18nService::getInstance();
    switch (type) {
    case ChestType::Locked:
        return i18n.get(
            "floating.locked",
            {
                {"owner", ownerName}
        }
        );
    case ChestType::RecycleShop:
        return i18n.get(
            "floating.recycle",
            {
                {"owner", ownerName}
        }
        );
    case ChestType::Shop:
        return i18n.get(
            "floating.shop",
            {
                {"owner", ownerName}
        }
        );
    case ChestType::Public:
        return i18n.get(
            "floating.public",
            {
                {"owner", ownerName}
        }
        );
    default:
        return i18n.get(
            "floating.unknown",
            {
                {"owner", ownerName}
        }
        );
    }
}

std::string TextService::generateDynamicShopText(ChestType type, const std::string& itemName) {
    auto& i18n = I18nService::getInstance();
    return type == ChestType::Shop ? i18n.get(
                                         "floating.shop_sell",
                                         {
                                             {"item", itemName}
    }
                                     )
                                   : i18n.get("floating.recycle_buy", {{"item", itemName}});
}

std::string TextService::generateEmptyShopText(ChestType type) {
    auto& i18n = I18nService::getInstance();
    return type == ChestType::Shop ? i18n.get("floating.shop_empty") : i18n.get("floating.recycle_empty");
}

std::string TextService::generateShopItemText(const std::string& itemName, double price, int stock, int chestStock) {
    return I18nService::getInstance().get(
        "floating.item_info",
        {
            {"item",        itemName                  },
            {"stock",       std::to_string(stock)     },
            {"chest_stock", std::to_string(chestStock)},
            {"price",       MoneyFormat::format(price)}
    }
    );
}

std::string TextService::getChestTypeName(ChestType type) {
    auto& i18n = I18nService::getInstance();
    switch (type) {
    case ChestType::Locked:
        return i18n.get("chest.type_locked");
    case ChestType::RecycleShop:
        return i18n.get("chest.type_recycle");
    case ChestType::Shop:
        return i18n.get("chest.type_shop");
    case ChestType::Public:
        return i18n.get("chest.type_public");
    default:
        return i18n.get("chest.type_unknown");
    }
}

} // namespace CT
