#include "TextService.h"
#include "Utils/MoneyFormat.h"

namespace CT {

TextService& TextService::getInstance() {
    static TextService instance;
    return instance;
}

TextService::TextService() { initMessages(); }

void TextService::initMessages() {
    // 箱子相关消息
    mMessages["chest.locked"]         = "§c这个箱子已经被锁定了，你无法打开。";
    mMessages["chest.not_owner"]      = "§c只有箱子主人才能进行此操作。";
    mMessages["chest.create_success"] = "§a箱子已成功设为{type}！花费 {price}";
    mMessages["chest.create_fail"]    = "§c设置{type}失败！已退还金钱";
    mMessages["chest.remove_success"] = "§a箱子设置已成功移除！";
    mMessages["chest.remove_fail"]    = "§c箱子设置移除失败！";
    mMessages["chest.no_permission"]  = "§c你没有创建该类型箱子的权限！";
    mMessages["chest.limit_reached"]  = "§c你已达到{type}的数量上限（{max}个）！";
    mMessages["chest.config_saved"]   = "§a箱子设置已保存！";
    mMessages["chest.config_fail"]    = "§c箱子设置保存失败！";

    // 商店相关消息
    mMessages["shop.insufficient_money"] = "§c你的金币不足！需要 §6{price}§c 金币。";
    mMessages["shop.insufficient_stock"] = "§c箱子中没有足够的商品！实际库存: {stock}";
    mMessages["shop.purchase_success"] = "§a购买成功！你花费了 §6{price}§a 金币购买了 {item} x{count}。";
    mMessages["shop.purchase_fail"]       = "§c购买失败！";
    mMessages["shop.purchase_chest_fail"] = "§c购买失败，箱子中物品数量不足！";
    mMessages["shop.purchase_money_fail"] = "§c购买失败，金币扣除失败。";
    mMessages["shop.item_not_exist"]      = "§c商品不存在！";
    mMessages["shop.price_set_success"]   = "§a物品价格设置成功！价格: {price}，数量: {count}";
    mMessages["shop.price_set_fail"]      = "§c物品价格设置失败！";
    mMessages["shop.item_def_fail"]       = "§c无法创建物品定义！";
    mMessages["shop.item_removed"]        = "§a商品已成功移除！";
    mMessages["shop.item_remove_fail"]    = "§c商品移除失败！";
    mMessages["shop.empty"]               = "商店是空的，没有可购买的商品。\n";
    mMessages["shop.name_set_success"]    = "§a商店名称设置成功！";
    mMessages["shop.name_set_fail"]       = "§c商店名称设置失败！";
    mMessages["shop.data_corrupt"]        = "§c该物品数据已损坏，无法购买。";
    mMessages["shop.item_manage_fail"]    = "§c无法管理该物品，无法从NBT创建物品。";
    mMessages["shop.item_id_fail"]        = "§c无法管理该物品，无法获取物品ID。";
    mMessages["shop.chest_empty"]         = "箱子是空的。\n";
    mMessages["shop.no_records"]          = "§7该商店暂无购买记录。";
    mMessages["shop.records_title"]       = "§a最近的购买记录:\n";
    mMessages["shop.purchase_id_fail"]    = "§c购买失败，无法获取商品ID。";

    // 回收商店消息
    mMessages["recycle.success"]        = "§a成功回收 {item} x{count}，获得 §6{price}§a 金币。";
    mMessages["recycle.fail"]           = "§c回收失败！";
    mMessages["recycle.commission_set"] = "§a回收委托设置成功！价格: {price}，最大回收数量: {max}";
    mMessages["recycle.commission_update"] = "§a委托信息更新成功！新价格: {price}，新最大回收数量: {max}";
    mMessages["recycle.no_commission"]      = "§c无法找到此回收委托。";
    mMessages["recycle.no_item_def"]        = "§c无法找到此回收委托的物品定义。";
    mMessages["recycle.not_recycle_shop"]   = "§c回收失败，此箱子不再是回收商店。";
    mMessages["recycle.owner_not_found"]    = "§c回收失败，找不到商店主人。";
    mMessages["recycle.owner_insufficient"] = "§c回收失败，商店主人余额不足。";
    mMessages["recycle.chest_full"]         = "§c回收失败，箱子空间不足，请清理箱子后再试。";
    mMessages["recycle.max_reached"]        = "§c回收失败，该委托已达到最大回收数量 ({max})。";
    mMessages["recycle.item_insufficient"]  = "§c回收失败，背包中可回收物品不足或中途消失。";
    mMessages["recycle.money_refund"]       = "§c回收失败，商店主人余额不足，物品已退回。";
    mMessages["recycle.db_fail"]            = "§c回收失败，数据库更新失败。";
    mMessages["recycle.chest_entity_fail"]  = "§c回收失败，无法获取箱子实体。";
    mMessages["recycle.item_id_fail"]       = "§c回收失败，无法获取物品ID。";
    mMessages["recycle.data_corrupt"]       = "§c该委托物品数据已损坏，无法回收。";
    mMessages["recycle.load_fail"]          = "§c无法加载物品信息。";
    mMessages["recycle.load_records_fail"]  = "§c无法加载回收记录。";
    mMessages["recycle.empty"]              = "该回收商店没有任何回收委托。";
    mMessages["recycle.list_content"]       = "以下是该回收商店的所有回收委托：";

    // 经济相关
    mMessages["economy.insufficient"] = "§c金钱不足！需要 {price}";
    mMessages["economy.deduct_fail"]  = "§c扣除金钱失败！";
    mMessages["economy.refund"]       = "已退还金钱";

    // 操作取消
    mMessages["action.cancelled"] = "§c你取消了操作。";

    // 分享相关
    mMessages["share.add_success"]      = "§a成功添加玩家 {player} 到分享列表！";
    mMessages["share.add_fail"]         = "§c添加分享玩家失败！";
    mMessages["share.remove_success"]   = "§a成功从分享列表移除玩家 {player}！";
    mMessages["share.remove_fail"]      = "§c移除分享玩家失败！";
    mMessages["share.player_not_found"] = "§c未找到玩家 {player}！";
    mMessages["share.name_empty"]       = "§c玩家名称不能为空！";

    // 输入验证
    mMessages["input.invalid_number"]    = "§c输入无效，请输入一个数字。";
    mMessages["input.invalid_count"]     = "§c购买数量必须大于0！";
    mMessages["input.invalid_buy_count"] = "§c购买数量输入无效，请输入一个正整数。";
    mMessages["input.negative_price"]    = "§c价格不能为负数！";
    mMessages["input.invalid_recycle_count"] = "§c回收数量无效！请输入一个介于1和{max}之间的整数。";
    mMessages["input.negative_durability"] = "§c最低耐久度不能为负数！";
    mMessages["input.negative_max_count"]  = "§c最大回收数量不能为负数！";
    mMessages["input.invalid_enchant"]     = "§c附魔格式无效，请使用 ID:等级,ID:等级 的格式。";
    mMessages["input.nbt_fail"]            = "§c无法获取物品NBT数据。";

    // 箱子实体相关
    mMessages["chest.entity_fail"] = "§c无法获取箱子数据。";

    // 传送相关
    mMessages["teleport.success"]            = "§a已传送到目标位置！花费 §6{cost}§a 金币。";
    mMessages["teleport.insufficient_money"] = "§c金币不足！传送需要 §6{cost}§c 金币。";
    mMessages["teleport.cooldown"]           = "§c传送冷却中！还需等待 §e{seconds}§c 秒。";
    mMessages["teleport.admin_success"]      = "§a已将你传送至箱子位置: {x}, {y}, {z}";
}

std::string TextService::getMessage(const std::string& key, const std::map<std::string, std::string>& params) {
    auto it = mMessages.find(key);
    if (it == mMessages.end()) {
        return "§c[未知消息: " + key + "]";
    }
    return replacePlaceholders(it->second, params);
}

std::string
TextService::replacePlaceholders(const std::string& text, const std::map<std::string, std::string>& params) {
    std::string result = text;
    for (const auto& [key, value] : params) {
        std::string placeholder = "{" + key + "}";
        size_t      pos         = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    return result;
}

std::string TextService::generateChestText(ChestType type, const std::string& ownerName) {
    switch (type) {
    case ChestType::Locked:
        return "§e[上锁箱子]§r 拥有者: " + ownerName;
    case ChestType::RecycleShop:
        return "§a[回收商店]§r 拥有者: " + ownerName;
    case ChestType::Shop:
        return "§b[商店箱子]§r 拥有者: " + ownerName;
    case ChestType::Public:
        return "§d[公共箱子]§r 拥有者: " + ownerName;
    default:
        return "§f[未知箱子类型]§r 拥有者: " + ownerName;
    }
}

std::string TextService::generateDynamicShopText(ChestType type, const std::string& itemName) {
    return (type == ChestType::Shop ? "§b[商店箱子]§r 出售: " : "§a[回收商店]§r 回收: ") + itemName;
}

std::string TextService::generateEmptyShopText(ChestType type) {
    return type == ChestType::Shop ? "§b[商店箱子]§r (无物品)" : "§a[回收商店]§r (无物品)";
}

std::string TextService::generateShopItemText(const std::string& itemName, double price, int stock, int chestStock) {
    return itemName + " §b[库存: " + std::to_string(stock) + "/" + std::to_string(chestStock) + "]§r"
         + " §6[价格: " + MoneyFormat::format(price) + "]§r";
}

std::string TextService::getChestTypeName(ChestType type) {
    switch (type) {
    case ChestType::Locked:
        return "普通锁";
    case ChestType::RecycleShop:
        return "回收商店";
    case ChestType::Shop:
        return "商店";
    case ChestType::Public:
        return "公共箱子";
    default:
        return "未知类型";
    }
}

} // namespace CT