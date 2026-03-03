#include "FormUtils.h"
#include "Config/ConfigManager.h"
#include "Utils/ItemTextureManager.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "compat/PermissionCompat.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/TeleportService.h"
#include "service/TextService.h"
#include <set>


namespace CT::FormUtils {

std::string getItemDisplayString(const ItemStack& item, int count, bool showTypeName) {
    auto&       i18n          = I18nService::getInstance();
    std::string displayString = std::string(item.getName());
    if (showTypeName) {
        displayString += " §7(" + item.getTypeName() + ")§r";
    }
    if (count > 0) {
        displayString += " x" + std::to_string(count);
    }

    // 显示耐久度
    if (item.isDamageableItem()) {
        int maxDamage      = item.getItem()->getMaxDamage();
        int currentDamage  = item.getDamageValue();
        displayString     += "\n"
                       + i18n.get(
                           "item_display.durability",
                           {
                               {"current", std::to_string(maxDamage - currentDamage)},
                               {"max",     std::to_string(maxDamage)                }
        }
                       );
    }

    // 显示特殊值
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        displayString += "\n"
                       + i18n.get(
                           "item_display.aux_value",
                           {
                               {"value", std::to_string(auxValue)}
        }
                       );
    }

    // 获取并显示附魔信息
    if (item.isEnchanted()) {
        ItemEnchants enchants    = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            displayString += i18n.get("item_display.enchantments");
            for (const auto& enchant : enchantList) {
                displayString +=
                    CT::NbtUtils::enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            displayString += "§r";
        }
    }

    // 如果是潜影盒，显示其内部物品
    if (item.getTypeName().find("shulker_box") != std::string::npos) {
        auto itemNbt = CT::NbtUtils::getItemNbt(item);
        if (itemNbt) {
            std::string shulkerContent = CT::NbtUtils::getShulkerBoxItems(*itemNbt);
            if (!shulkerContent.empty()) {
                displayString += i18n.get(
                    "item_display.shulker_contains",
                    {
                        {"items", shulkerContent}
                }
                );
            }
        }
    }

    // 如果是收纳袋，显示其内部物品
    if (item.getTypeName().find("bundle") != std::string::npos) {
        auto itemNbt = CT::NbtUtils::getItemNbt(item);
        if (itemNbt) {
            std::string bundleContent = CT::NbtUtils::getBundleItems(*itemNbt);
            if (!bundleContent.empty()) {
                displayString += i18n.get(
                    "item_display.bundle_contains",
                    {
                        {"items", bundleContent}
                }
                );
            }
        }
    }
    return displayString;
}

std::string getItemTexturePath(const ItemStack& item) {
    auto& textureManager = CT::ItemTextureManager::getInstance();
    short auxValue       = item.getAuxValue();

    auto lookupTexture = [&](const std::string& key) -> std::string {
        if (key.empty()) {
            return {};
        }

        if (key.rfind("textures/", 0) == 0) {
            return key;
        }

        if (auto path = textureManager.getTexture(key, auxValue); !path.empty()) {
            return path;
        }

        if (key.rfind("minecraft:", 0) == 0) {
            if (auto path = textureManager.getTexture(key.substr(10), auxValue); !path.empty()) {
                return path;
            }
        }

        auto colonPos = key.find(':');
        if (colonPos != std::string::npos && colonPos + 1 < key.size()) {
            if (auto path = textureManager.getTexture(key.substr(colonPos + 1), auxValue); !path.empty()) {
                return path;
            }
        }

        return {};
    };

    if (auto itemDef = item.getItem(); itemDef != nullptr) {
        std::string iconKey = itemDef->mIconName;
        if (auto path = lookupTexture(iconKey); !path.empty()) {
            return path;
        }

        std::string atlasKey = itemDef->mAtlasName;
        if (auto path = lookupTexture(atlasKey); !path.empty()) {
            return path;
        }
    }

    if (auto path = lookupTexture(item.getRawNameId()); !path.empty()) {
        return path;
    }

    return lookupTexture(item.getTypeName());
}

std::unique_ptr<ItemStack> createItemStackFromNbtString(const std::string& itemNbtStr) {
    auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
    if (!itemNbt) {
        logger.error("createItemStackFromNbtString: 无法解析物品NBT: {}", itemNbtStr);
        return nullptr;
    }
    itemNbt->at("Count") = ByteTag(1); // 从NBT创建物品需要Count标签
    auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!itemPtr) {
        logger.error("createItemStackFromNbtString: 无法从NBT创建物品。原始NBT: {}", itemNbtStr);
        return nullptr;
    }
    return itemPtr;
}

int countItemsInChest(BlockSource& region, BlockPos pos, int dimId, const std::string& targetItemNbtStr) {
    auto* blockActor = region.getBlockEntity(pos);
    int   totalCount = 0;
    if (!blockActor) {
        logger.warn("countItemsInChest: 无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return 0;
    }

    // 使用 mType 成员变量进行类型检查
    if (blockActor->mType != BlockActorType::Chest) {
        logger.error(
            "countItemsInChest: BlockActor 不是箱子类型在 ({}, {}, {}) in dim {}，实际类型: {}",
            pos.x,
            pos.y,
            pos.z,
            dimId,
            static_cast<int>(blockActor->mType)
        );
        return 0;
    }

    auto* chest = static_cast<ChestBlockActor*>(blockActor);

    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& chestItemInSlot = chest->getItem(i);
        if (!chestItemInSlot.isNull()) {
            auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
            if (chestItemNbt) {
                auto        cleanedChestItemNbt = CT::NbtUtils::cleanNbtForComparison(*chestItemNbt);
                std::string currentItemNbtStr   = CT::NbtUtils::toSNBT(*cleanedChestItemNbt);
                if (currentItemNbtStr == targetItemNbtStr) {
                    totalCount += chestItemInSlot.mCount;
                }
            }
        }
    }
    return totalCount;
}

void showSetNameForm(
    Player&                                     player,
    BlockPos                                    pos,
    int                                         dimId,
    const std::string&                          title,
    std::function<void(Player&, BlockPos, int)> onComplete
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(title);
    auto& txt = TextService::getInstance();
    if (!PermissionCompat::hasPermission(player.getUuid().asString(), "chest.admin")) {
        player.sendMessage(txt.getMessage("command.no_permission"));
        if (onComplete) onComplete(player, pos, dimId);
        return;
    }

    std::string currentName = ChestService::getInstance().getShopName(pos, dimId, player.getDimensionBlockSource());
    std::string nameDisplay = currentName.empty() ? i18n.get("form_utils.name_not_set") : "§a" + currentName;
    fm.appendLabel(i18n.get(
        "form_utils.current_shop_name",
        {
            {"name", nameDisplay}
    }
    ));
    fm.appendInput("shop_name", i18n.get("form_utils.input_shop_name"), "", currentName);

    fm.sendTo(
        player,
        [pos, dimId, onComplete](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& txt = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                if (onComplete) onComplete(p, pos, dimId);
                return;
            }

            std::string newName = std::get<std::string>(result.value().at("shop_name"));
            auto&       region  = p.getDimensionBlockSource();
            if (ChestService::getInstance().setShopName(pos, dimId, region, newName)) {
                p.sendMessage(txt.getMessage("shop.name_set_success"));
            } else {
                p.sendMessage(txt.getMessage("shop.name_set_fail"));
            }
            if (onComplete) onComplete(p, pos, dimId);
        }
    );
}

bool teleportToShop(Player& player, BlockPos pos, int dimId) {
    auto&       tpService  = TeleportService::getInstance();
    auto&       txt        = TextService::getInstance();
    auto&       config     = ConfigManager::getInstance().get();
    std::string playerUuid = player.getUuid().asString();

    if (!tpService.canTeleport(playerUuid)) {
        int remainingSeconds = tpService.getRemainingCooldown(playerUuid);
        player.sendMessage(txt.getMessage(
            "teleport.cooldown",
            {
                {"seconds", std::to_string(remainingSeconds)}
        }
        ));
        return false;
    }

    double tpCost = config.teleportSettings.teleportCost;
    if (!Economy::hasMoney(player, tpCost)) {
        player.sendMessage(txt.getMessage(
            "teleport.insufficient_money",
            {
                {"cost", MoneyFormat::format(tpCost)}
        }
        ));
        return false;
    }

    if (!Economy::reduceMoney(player, tpCost)) {
        player.sendMessage(txt.getMessage("economy.deduct_fail"));
        return false;
    }

    player.teleport({(float)pos.x + 0.5f, (float)pos.y + 1.0f, (float)pos.z + 0.5f}, dimId);
    tpService.recordTeleport(playerUuid);
    player.sendMessage(txt.getMessage(
        "teleport.success",
        {
            {"cost", MoneyFormat::format(tpCost)}
    }
    ));
    return true;
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

std::map<std::string, std::string> getPlayerNameCache(const std::vector<std::string>& uuids) {
    std::map<std::string, std::string> cache;
    auto&                              i18n = I18nService::getInstance();
    auto&                              info = ll::service::PlayerInfo::getInstance();

    std::set<std::string> uniqueUuids(uuids.begin(), uuids.end());

    for (const auto& uuid : uniqueUuids) {
        if (uuid.empty()) continue;
        auto playerInfo = info.fromUuid(mce::UUID::fromString(uuid));
        cache[uuid]     = playerInfo ? playerInfo->name : i18n.get("public_shop.unknown_owner");
    }
    return cache;
}

} // namespace CT::FormUtils
