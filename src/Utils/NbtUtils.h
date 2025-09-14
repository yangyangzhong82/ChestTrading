#pragma once

#include "mc/dataloadhelper/DataLoadHelper.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/nbt/Tag.h"
#include <memory>
#include <string>

// Forward declaration
class Player;

// Forward declaration
class BlockActor;
// Forward declaration
class ItemStack;

namespace CauldronZero::NbtUtils {
class MoreGlobal {
public:
    static DataLoadHelper& defaultDataLoadHelper();
};
/**
 * @brief 获取在线玩家的NBT数据
 * @param player 指向玩家对象的指针
 * @return 一个包含玩家NBT数据的 std::unique_ptr<CompoundTag>。如果玩家指针为空，则返回 nullptr。
 */
 std::unique_ptr<CompoundTag> getOnlinePlayerNbt(Player* player);

/**
 * @brief 设置在线玩家的NBT数据
 * @param player 指向玩家对象的指针
 * @param nbtTag 一个包含要设置的NBT数据的 std::unique_ptr<CompoundTag>
 * @return 如果设置成功，返回 true；否则返回 false。
 */
 bool setOnlinePlayerNbt(Player* player, std::unique_ptr<CompoundTag> nbtTag);

/**
 * @brief 获取离线玩家的NBT数据
 * @param uuidString 玩家的UUID字符串
 * @return 一个包含玩家NBT数据的 std::unique_ptr<CompoundTag>。如果找不到或发生错误，则返回 nullptr。
 */
 std::unique_ptr<CompoundTag> getOfflinePlayerNbt(const std::string& uuidString);

/**
 * @brief 设置离线玩家的NBT数据
 * @param uuidString 玩家的UUID字符串
 * @param nbtTag 一个包含要设置的NBT数据的 std::unique_ptr<CompoundTag>
 * @return 如果设置成功，返回 true；否则返回 false。
 */
 bool setOfflinePlayerNbt(const std::string& uuidString, std::unique_ptr<CompoundTag> nbtTag);

/**
 * @brief 在指定的CompoundTag中设置一个NBT标签
 * @param comp 指向目标CompoundTag的指针
 * @param key 要设置的NBT的键
 * @param tag 要设置的NBT标签 (std::unique_ptr<Tag>)
 * @return 如果设置成功，返回 true；否则返回 false。
 */
 bool setTag(CompoundTag* comp, const std::string& key, std::unique_ptr<Tag> tag);

/**
 * @brief 从指定的CompoundTag中获取一个NBT标签
 * @param comp 指向目标CompoundTag的指针
 * @param key 要获取的NBT的键
 * @return 一个包含NBT标签深拷贝的 std::unique_ptr<Tag>。如果键不存在或发生错误，则返回 nullptr。
 */
 std::unique_ptr<Tag> getTag(CompoundTag* comp, const std::string& key);

/**
 * @brief 将SNBT字符串解析为CompoundTag
 * @param snbt SNBT字符串
 * @return 一个包含解析后数据的 std::unique_ptr<CompoundTag>。如果解析失败，则返回 nullptr。
 */
 std::unique_ptr<CompoundTag> parseSNBT(const std::string& snbt);

/**
 * @brief 将CompoundTag转换为SNBT字符串
 * @param tag 要转换的CompoundTag
 * @param indent 缩进空格数，-1表示不格式化
 * @return SNBT字符串。
 */
 std::string toSNBT(const CompoundTag& tag, int indent = -1);

/**
 * @brief 将二进制NBT数据解析为CompoundTag
 * @param data 二进制NBT数据
 * @return 一个包含解析后数据的 std::unique_ptr<CompoundTag>。如果解析失败，则返回 nullptr。
 */
 std::unique_ptr<CompoundTag> parseBinaryNBT(std::string_view data);

/**
 * @brief 将CompoundTag转换为二进制NBT数据
 * @param tag 要转换的CompoundTag
 * @return 包含二进制NBT数据的字符串。
 */


 std::string toBinaryNBT(const CompoundTag& tag);

/**
 * @brief 获取方块实体的NBT数据
 * @param blockEntity 指向方块实体对象的指针
 * @return 一个包含NBT数据的 std::unique_ptr<CompoundTag>。如果指针为空，则返回 nullptr。
 */
std::unique_ptr<CompoundTag> getBlockEntityNbt(BlockActor* blockEntity);

/**
 * @brief 设置方块实体的NBT数据
 * @param blockEntity 指向方块实体对象的指针
 * @param nbtTag 一个包含要设置的NBT数据的 std::unique_ptr<CompoundTag>
 * @return 如果设置成功，返回 true；否则返回 false。
 */
 bool setBlockEntityNbt(BlockActor* blockEntity, const CompoundTag& nbtTag);
/**
 * @brief 从NBT数据创建一个新的ItemStack
 * @param tag 包含物品数据的CompoundTag
 * @return 一个包含新物品的 std::unique_ptr<ItemStack>。
 */
  std::unique_ptr<ItemStack> createItemFromNbt(const CompoundTag& tag);

/**
 * @brief 获取ItemStack的NBT数据
 * @param item 要获取NBT的ItemStack
 * @return 一个包含NBT数据的 std::unique_ptr<CompoundTag>。
 */
 std::unique_ptr<CompoundTag> getItemNbt(const ItemStack& item);

/**
 * @brief 设置ItemStack的NBT数据
 * @param item 要设置NBT的ItemStack
 * @param tag 包含要设置的NBT数据的CompoundTag
 * @return 如果设置成功，返回 true；否则返回 false。
 */
 bool setItemNbt(ItemStack& item, const CompoundTag& tag);

} // namespace CauldronZero::NbtUtils
