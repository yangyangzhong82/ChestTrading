#include "logger.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/nbt/ByteArrayTag.h"
#include "mc/nbt/ByteTag.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/nbt/DoubleTag.h"
#include "mc/nbt/EndTag.h"
#include "mc/nbt/FloatTag.h"
#include "mc/nbt/Int64Tag.h"
#include "mc/nbt/IntArrayTag.h"
#include "mc/nbt/IntTag.h"
#include "mc/nbt/ListTag.h"
#include "mc/nbt/ShortTag.h"
#include "mc/nbt/StringTag.h"
#include "mc/nbt/Tag.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/storage/DBStorage.h"
#include "mc/world/level/storage/DBStorageConfig.h"
#include "mc/world/level/storage/db_helpers/Category.h" // 新增
#include "mc\world\item\SaveContextFactory.h"
#include <string_view>


namespace   CT::NbtUtils {
DBStorage*                                                  dbStorage;
std::unordered_map<mce::UUID, std::unique_ptr<CompoundTag>> playerNbtCache; // 定义玩家NBT缓存
LL_AUTO_TYPE_INSTANCE_HOOK(
    DBStorageHook,
    HookPriority::Normal,
    DBStorage,
    &DBStorage::$ctor,
    void*,
    ::DBStorageConfig                           config,
    ::Bedrock::NotNullNonOwnerPtr<::LevelDbEnv> levelDbEnv
) {
    void* ori = origin(std::move(config), levelDbEnv);
    dbStorage = (DBStorage*)ori;
    return ori;
};

// 实现获取离线玩家NBT的函数
std::unique_ptr<CompoundTag> getOfflinePlayerNbt(const std::string& uuidString) {
    logger.debug("尝试获取离线玩家NBT，UUID: {}", uuidString);
    if (!dbStorage) {
        logger.error("DBStorage 未初始化，无法获取离线玩家NBT。");
        return nullptr;
    }

    mce::UUID uuid = mce::UUID::fromString(uuidString);
    if (uuid == mce::UUID::EMPTY()) {
        logger.error("无效的玩家UUID: {}", uuidString);
        return nullptr; // 无效的UUID
    }

    // 尝试从数据库获取玩家元数据
    std::string playerKey = "player_" + uuid.asString();
    if (dbStorage->hasKey(playerKey, DBHelpers::Category::Player)) {
        logger.debug("在数据库中找到玩家元数据键: {}", playerKey);
        std::unique_ptr<CompoundTag> playerTag = dbStorage->getCompoundTag(playerKey, DBHelpers::Category::Player);
        if (playerTag) {
            if (playerTag->contains("ServerId")) {
                std::string serverId = playerTag->at("ServerId").get<StringTag>();
                if (!serverId.empty()) {
                    logger.debug("获取到ServerId: {}", serverId);
                    if (dbStorage->hasKey(serverId, DBHelpers::Category::Player)) {
                        // 使用ServerId获取实际的玩家NBT数据
                        logger.debug("使用ServerId获取实际玩家NBT数据: {}", serverId);
                        return dbStorage->getCompoundTag(serverId, DBHelpers::Category::Player);
                    } else {
                        logger.warn("数据库中未找到ServerId对应的玩家NBT数据: {}", serverId);
                    }
                } else {
                    logger.warn("玩家元数据中ServerId为空。");
                }
            } else {
                logger.warn("玩家元数据中不包含ServerId。");
            }
        } else {
            logger.error("无法从数据库获取玩家元数据，键: {}", playerKey);
        }
    } else {
        logger.debug("数据库中未找到玩家元数据键: {}", playerKey);
    }
    logger.debug("获取离线玩家NBT失败，UUID: {}", uuidString);
    return nullptr;
}

// 实现设置离线玩家NBT的函数
bool setOfflinePlayerNbt(const std::string& uuidString, std::unique_ptr<CompoundTag> nbtTag) {
    logger.debug("尝试设置玩家NBT，UUID: {}", uuidString);
    if (!nbtTag) {
        logger.error("传入的NBT标签为空，无法设置玩家NBT。");
        return false;
    }
    if (!dbStorage) {
        logger.error("DBStorage 未初始化，无法设置玩家NBT。");
        return false;
    }

    mce::UUID uuid = mce::UUID::fromString(uuidString);
    if (uuid == mce::UUID::EMPTY()) {
        logger.error("无效的玩家UUID: {}", uuidString);
        return false; // 无效的UUID
    }

    // 尝试获取在线玩家，如果在线则直接更新
    Player* player = ll::service::getLevel()->getPlayer(uuid);
    if (player) {
        logger.debug("玩家 {} 在线，直接加载NBT数据。", player->getRealName());
        DefaultDataLoadHelper dataLoadHelper;
        player->load(*nbtTag, dataLoadHelper);
        return true;
    } else {
        logger.debug("玩家 {} 不在线，尝试通过数据库更新NBT数据。", uuidString);
        // 玩家不在线，通过数据库更新
        std::string playerKey = "player_" + uuid.asString();
        if (dbStorage->hasKey(playerKey, DBHelpers::Category::Player)) {
            logger.debug("在数据库中找到玩家元数据键: {}", playerKey);
            std::unique_ptr<CompoundTag> playerMetadata =
                dbStorage->getCompoundTag(playerKey, DBHelpers::Category::Player);
            if (playerMetadata) {
                if (playerMetadata->contains("ServerId")) {
                    std::string serverId = playerMetadata->at("ServerId").get<StringTag>();
                    if (!serverId.empty()) {
                        logger.debug("获取到ServerId: {}，保存NBT数据。", serverId);
                        // 将CompoundTag转换为二进制NBT格式并保存
                        dbStorage->saveData(serverId, nbtTag->toBinaryNbt(), DBHelpers::Category::Player);
                        logger.debug("NBT数据已保存到数据库。");
                        return true; // 假设保存操作成功，模仿LSE行为
                    } else {
                        logger.warn("玩家元数据中ServerId为空，无法保存NBT。");
                    }
                } else {
                    logger.warn("玩家元数据中不包含ServerId，无法保存NBT。");
                }
            } else {
                logger.error("无法从数据库获取玩家元数据，键: {}", playerKey);
            }
        } else {
            logger.warn("数据库中未找到玩家元数据键: {}，无法保存NBT。", playerKey);
        }
    }
    logger.debug("设置玩家NBT失败，UUID: {}", uuidString);
    return false;
}

// 实现设置键对应的 NBT 对象
bool setTag(CompoundTag* comp, const std::string& key, std::unique_ptr<Tag> tag) {
    logger.debug("尝试在CompoundTag中设置键 '{}' 的NBT。", key);
    if (!comp) {
        logger.error("传入的CompoundTag指针为空，无法设置NBT。");
        return false;
    }
    if (!tag) {
        logger.error("传入的NBT标签为空，无法设置键 '{}' 的NBT。", key);
        return false;
    }


    switch (tag->getId()) {
    case Tag::Type::Byte:
        comp->at(key) = tag->as<ByteTag>();
        break;
    case Tag::Type::Short:
        comp->at(key) = tag->as<ShortTag>();
        break;
    case Tag::Type::Int:
        comp->at(key) = tag->as<IntTag>();
        break;
    case Tag::Type::Int64:
        comp->at(key) = tag->as<Int64Tag>();
        break;
    case Tag::Type::Float:
        comp->at(key) = tag->as<FloatTag>();
        break;
    case Tag::Type::Double:
        comp->at(key) = tag->as<DoubleTag>();
        break;
    case Tag::Type::String:
        comp->at(key) = tag->as<StringTag>();
        break;
    case Tag::Type::ByteArray:
        comp->at(key) = tag->as<ByteArrayTag>();
        break;
    case Tag::Type::List:
        comp->at(key) = tag->as<ListTag>();
        break;
    case Tag::Type::Compound:
        comp->at(key) = tag->as<CompoundTag>();
        break;
    case Tag::Type::IntArray:
        comp->at(key) = tag->as<IntArrayTag>();
        break;
    case Tag::Type::End:
    default:
        logger.warn("尝试设置未知或End类型的NBT标签到键 '{}'。", key);
        return false;
    }
    logger.debug("成功在CompoundTag中设置键 '{}' 的NBT。", key);
    return true;
}

// 实现读取键对应的 NBT 对象
std::unique_ptr<Tag> getTag(CompoundTag* comp, const std::string& key) {
    logger.debug("尝试从CompoundTag中读取键 '{}' 的NBT。", key);
    if (!comp) {
        logger.error("传入的CompoundTag指针为空，无法读取NBT。");
        return nullptr;
    }

    try {
        // 使用 CompoundTag::at() 访问，如果键不存在会抛出 std::out_of_range
        CompoundTagVariant& variant = comp->at(key);
        logger.debug("成功从CompoundTag中找到键 '{}' 的NBT，类型为: {}", key, static_cast<int>(variant.getId()));
        // 返回 Tag 的深拷贝，确保调用者拥有独立的所有权
        return variant.get().copy();
    } catch (const std::out_of_range& e) {
        logger.warn("CompoundTag中键 '{}' 不存在。错误: {}", key, e.what());
        return nullptr; // 键不存在，返回 nullptr
    } catch (const std::exception& e) {
        logger.error("读取CompoundTag中键 '{}' 的NBT时发生未知错误: {}", key, e.what());
        return nullptr;
    }
}

// 实现获取在线玩家NBT的函数
std::unique_ptr<CompoundTag> getOnlinePlayerNbt(Player* player) {
    logger.debug("尝试获取在线玩家NBT，玩家: {}", player->getRealName());
    if (!player) {
        logger.error("传入的玩家指针为空，无法获取在线玩家NBT。");
        return nullptr;
    }
    // 获取玩家的NBT数据
    std::unique_ptr<CompoundTag> nbt = std::make_unique<CompoundTag>();
    player->save(*nbt);
    logger.debug("成功获取在线玩家 {} 的NBT。", player->getRealName());
    return nbt;
}

// 实现设置在线玩家NBT的函数
bool setOnlinePlayerNbt(Player* player, std::unique_ptr<CompoundTag> nbtTag) {
    logger.debug("尝试设置在线玩家NBT，玩家: {}", player->getRealName());
    if (!player) {
        logger.error("传入的玩家指针为空，无法设置在线玩家NBT。");
        return false;
    }
    if (!nbtTag) {
        logger.error("传入的NBT标签为空，无法设置在线玩家NBT。");
        return false;
    }
    DefaultDataLoadHelper dataLoadHelper;
    player->load(*nbtTag, dataLoadHelper);
    logger.debug("成功设置在线玩家 {} 的NBT。", player->getRealName());
    return true;
}


std::unique_ptr<CompoundTag> parseSNBT(const std::string& snbt) {
    auto tag = CompoundTag::fromSnbt(snbt);
    if (tag.has_value()) {
        return tag->clone();
    }
    logger.warn("从SNBT解析CompoundTag失败: {}", snbt);
    return nullptr;
}

std::string toSNBT(const CompoundTag& tag, int indent) {
    if (indent == -1) {
        return tag.toSnbt(SnbtFormat::ForceQuote, 0);
    } else {
        return tag.toSnbt(SnbtFormat::PartialLineFeed, indent);
    }
}

std::unique_ptr<CompoundTag> parseBinaryNBT(std::string_view data) {
    auto tag = CompoundTag::fromBinaryNbt(data);
    if (tag.has_value()) {
        return tag->clone();
    }
    logger.warn("从二进制数据解析CompoundTag失败。");
    return nullptr;
}

std::string toBinaryNBT(const CompoundTag& tag) { return tag.toBinaryNbt(); }

std::unique_ptr<CompoundTag> getBlockEntityNbt(BlockActor* blockEntity) {
    if (!blockEntity) {
        return nullptr;
    }
    auto tag = std::make_unique<CompoundTag>();
    blockEntity->save(*tag, *SaveContextFactory::createCloneSaveContext());
    return tag;
}

bool setBlockEntityNbt(BlockActor* blockEntity, const CompoundTag& nbtTag) {
    if (!blockEntity) {
        return false;
    }
    DefaultDataLoadHelper helper;
    blockEntity->load(*ll::service::getLevel(), nbtTag, helper);
    return true;
}

std::unique_ptr<ItemStack> createItemFromNbt(const CompoundTag& tag) {
    auto newItem = std::make_unique<ItemStack>(ItemStack::EMPTY_ITEM());
    newItem->_loadItem(tag);
    auto mItem = newItem->mItem;
    if (mItem) {
        mItem->fixupCommon(*newItem);
        if (newItem->getAuxValue() == 0x7FFF) {
            newItem->mAuxValue = 0;
        }
    }
    return newItem;
}


std::unique_ptr<CompoundTag> getItemNbt(const ItemStack& item) {
    return item.save(*SaveContextFactory::createCloneSaveContext());
}

bool setItemNbt(ItemStack& item, const CompoundTag& tag) {
    item._loadItem(tag);
    auto mItem = item.mItem;
    if (mItem) {
        mItem->fixupCommon(item);
        if (item.getAuxValue() == 0x7FFF) {
            item.mAuxValue = 0;
        }
    }
    return true;
}


} // namespace CauldronZero::NbtUtils
