#include "FormUtils.h"
#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"


namespace CT::FormUtils {

std::string getItemDisplayString(const ItemStack& item, int count, bool showTypeName) {
    std::string displayString = std::string(item.getName());
    if (showTypeName) {
        displayString += " §7(" + item.getTypeName() + ")§r";
    }
    if (count > 0) {
        displayString += " x" + std::to_string(count);
    }

    // 显示耐久度
    if (item.isDamageableItem()) {
        int maxDamage     = item.getItem()->getMaxDamage();
        int currentDamage = item.getDamageValue();
        displayString += "\n§a耐久: " + std::to_string(maxDamage - currentDamage) + " / " + std::to_string(maxDamage);
    }

    // 显示特殊值
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        displayString += "\n§e特殊值: " + std::to_string(auxValue);
    }

    // 获取并显示附魔信息
    if (item.isEnchanted()) {
        ItemEnchants enchants    = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            displayString += "\n§d附魔: ";
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
                displayString += "\n§7内含: " + shulkerContent + "§r";
            }
        }
    }

    // 如果是收纳袋，显示其内部物品
    if (item.getTypeName().find("bundle") != std::string::npos) {
        auto itemNbt = CT::NbtUtils::getItemNbt(item);
        if (itemNbt) {
            std::string bundleContent = CT::NbtUtils::getBundleItems(*itemNbt);
            if (!bundleContent.empty()) {
                displayString += "\n§7内含: " + bundleContent + "§r";
            }
        }
    }
    return displayString;
}

std::string getItemTexturePath(const ItemStack& item) {
    std::string itemName = item.getTypeName();
    if (itemName.rfind("minecraft:", 0) == 0) {
        itemName = itemName.substr(10);
    }
    return CT::ItemTextureManager::getInstance().getTexture(itemName, item.getAuxValue());
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

} // namespace CT::FormUtils
