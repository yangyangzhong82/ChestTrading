#include "I18nService.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace CT {

I18nService& I18nService::getInstance() {
    static I18nService instance;
    return instance;
}

bool I18nService::load(const std::string& langDir, const std::string& lang) {
    mLangDir     = langDir;
    mCurrentLang = lang;

    // 确保目录存在
    if (!std::filesystem::exists(langDir)) {
        std::filesystem::create_directories(langDir);
    }

    // 创建默认语言文件
    createDefaultLangFiles(langDir);

    // 加载语言文件
    std::string langFile = langDir + "/" + lang + ".json";
    if (!std::filesystem::exists(langFile)) {
        logger.warn("Language file not found: {}, falling back to zh_CN", langFile);
        langFile     = langDir + "/zh_CN.json";
        mCurrentLang = "zh_CN";
    }

    std::ifstream file(langFile);
    if (!file.is_open()) {
        logger.error("Failed to open language file: {}", langFile);
        return false;
    }

    try {
        nlohmann::json json = nlohmann::json::parse(file, nullptr, true, true);
        mMessages.clear();

        // 递归展平 JSON
        std::function<void(const nlohmann::json&, const std::string&)> flatten = [&](const nlohmann::json& j,
                                                                                     const std::string&    prefix) {
            for (auto& [key, value] : j.items()) {
                std::string fullKey = prefix.empty() ? key : prefix + "." + key;
                if (value.is_object()) {
                    flatten(value, fullKey);
                } else if (value.is_string()) {
                    mMessages[fullKey] = value.get<std::string>();
                }
            }
        };
        flatten(json, "");

        logger.info("Loaded {} translations from {}", mMessages.size(), langFile);
        return true;
    } catch (const std::exception& e) {
        logger.error("Failed to parse language file: {}", e.what());
        return false;
    }
}

std::string I18nService::get(const std::string& key, const std::map<std::string, std::string>& params) const {
    auto it = mMessages.find(key);
    if (it == mMessages.end()) {
        return "§c[Missing: " + key + "]";
    }
    return replacePlaceholders(it->second, params);
}

std::string
I18nService::replacePlaceholders(const std::string& text, const std::map<std::string, std::string>& params) const {
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

void I18nService::createDefaultLangFiles(const std::string& langDir) {
    // 中文语言文件
    std::string zhFile = langDir + "/zh_CN.json";
    if (!std::filesystem::exists(zhFile)) {
        nlohmann::json zh = {
            {"chest",
             {{"locked", "§c这个箱子已经被锁定了，你无法打开。"},
              {"not_owner", "§c只有箱子主人才能进行此操作。"},
              {"create_success", "§a箱子已成功设为{type}！花费 {price}"},
              {"create_fail", "§c设置{type}失败！已退还金钱"},
              {"remove_success", "§a箱子设置已成功移除！"},
              {"remove_fail", "§c箱子设置移除失败！"},
              {"no_permission", "§c你没有创建该类型箱子的权限！"},
              {"limit_reached", "§c你已达到{type}的数量上限（{max}个）！"},
              {"config_saved", "§a箱子设置已保存！"},
              {"config_fail", "§c箱子设置保存失败！"},
              {"entity_fail", "§c无法获取箱子数据。"},
              {"set_fail_transaction", "§c箱子设置失败：无法开始事务！"},
              {"set_fail", "§c箱子设置失败！"},
              {"set_fail_commit", "§c箱子设置失败：事务提交失败！"},
              {"set_success", "§a箱子设置成功！"},
              {"remove_fail_db", "§c箱子设置移除失败！"},
              {"unknown_type", "§c未知的箱子类型！"},
              {"type_locked", "上锁箱子"},
              {"type_public", "公共箱子"},
              {"type_recycle", "回收商店"},
              {"type_shop", "商店"},
              {"type_unknown", "未知类型"},
              {"name_locked", "的上锁箱子"},
              {"name_public", "的公共箱子"}}                                                                         },
            {"shop",
             {{"insufficient_money", "§c你的金币不足！需要 §6{price}§c 金币。"},
              {"insufficient_stock", "§c箱子中没有足够的商品！实际库存: {stock}"},
              {"purchase_success", "§a购买成功！你花费了 §6{price}§a 金币购买了 {item} x{count}。"},
              {"purchase_fail", "§c购买失败！"},
              {"purchase_chest_fail", "§c购买失败，箱子中物品数量不足！"},
              {"purchase_money_fail", "§c购买失败，金币扣除失败。"},
              {"purchase_db_fail", "§c购买失败，数据库更新失败。"},
              {"purchase_id_fail", "§c购买失败，无法获取商品ID。"},
              {"item_not_exist", "§c商品不存在！"},
              {"price_set_success", "§a物品价格设置成功！价格: {price}，数量: {count}"},
              {"price_set_fail", "§c物品价格设置失败！"},
              {"item_def_fail", "§c无法创建物品定义！"},
              {"item_removed", "§a商品已成功移除！"},
              {"item_remove_fail", "§c商品移除失败！"},
              {"empty", "商店是空的，没有可购买的商品。\n"},
              {"name_set_success", "§a商店名称设置成功！"},
              {"name_set_fail", "§c商店名称设置失败！"},
              {"data_corrupt", "§c该物品数据已损坏，无法购买。"},
              {"item_manage_fail", "§c无法管理该物品，无法从NBT创建物品。"},
              {"item_id_fail", "§c无法管理该物品，无法获取物品ID。"},
              {"chest_empty", "箱子是空的。\n"},
              {"no_records", "§7该商店暂无购买记录。"},
              {"records_title", "§a最近的购买记录:\n"},
              {"unknown_item", "未知物品"}}                                                                           },
            {"recycle",
             {{"success", "§a成功回收 {item} x{count}，获得 §6{price}§a 金币。"},
              {"fail", "§c回收失败！"},
              {"commission_set", "§a回收委托设置成功！价格: {price}，最大回收数量: {max}"},
              {"commission_update", "§a委托信息更新成功！新价格: {price}，新最大回收数量: {max}"},
              {"no_commission", "§c无法找到此回收委托。"},
              {"no_item_def", "§c无法找到此回收委托的物品定义。"},
              {"not_recycle_shop", "§c回收失败，此箱子不再是回收商店。"},
              {"owner_not_found", "§c回收失败，找不到商店主人。"},
              {"owner_insufficient", "§c回收失败，商店主人余额不足。"},
              {"chest_full", "§c回收失败，箱子空间不足，请清理箱子后再试。"},
              {"max_reached", "§c回收失败，该委托已达到最大回收数量 ({max})。"},
              {"item_insufficient", "§c回收失败，背包中可回收物品不足或中途消失。"},
              {"money_refund", "§c回收失败，商店主人余额不足，物品已退回。"},
              {"db_fail", "§c回收失败，数据库更新失败。"},
              {"chest_entity_fail", "§c回收失败，无法获取箱子实体。"},
              {"item_id_fail", "§c回收失败，无法获取物品ID。"},
              {"data_corrupt", "§c该委托物品数据已损坏，无法回收。"},
              {"load_fail", "§c无法加载物品信息。"},
              {"load_records_fail", "§c无法加载回收记录。"},
              {"empty", "该回收商店没有任何回收委托。"},
              {"list_content", "以下是该回收商店的所有回收委托："}}                                       },
            {"economy",
             {{"insufficient", "§c金钱不足！需要 {price}"},
              {"deduct_fail", "§c扣除金钱失败！"},
              {"refund", "已退还金钱"}}                                                                              },
            {"action",   {{"cancelled", "§c你取消了操作。"}}                                                      },
            {"share",
             {{"add_success", "§a成功添加玩家 {player} 到分享列表！"},
              {"add_fail", "§c添加分享玩家失败！"},
              {"remove_success", "§a成功从分享列表移除玩家 {player}！"},
              {"remove_fail", "§c移除分享玩家失败！"},
              {"player_not_found", "§c未找到玩家 {player}！"},
              {"name_empty", "§c玩家名称不能为空！"}}                                                           },
            {"input",
             {{"invalid_number", "§c输入无效，请输入一个数字。"},
              {"invalid_count", "§c购买数量必须大于0！"},
              {"invalid_buy_count", "§c购买数量输入无效，请输入一个正整数。"},
              {"negative_price", "§c价格不能为负数！"},
              {"invalid_recycle_count", "§c回收数量无效！请输入一个介于1和{max}之间的整数。"},
              {"negative_durability", "§c最低耐久度不能为负数！"},
              {"negative_max_count", "§c最大回收数量不能为负数！"},
              {"invalid_enchant", "§c附魔格式无效，请使用 ID:等级,ID:等级 的格式。"},
              {"nbt_fail", "§c无法获取物品NBT数据。"}}                                                          },
            {"teleport",
             {{"success", "§a已传送到目标位置！花费 §6{cost}§a 金币。"},
              {"insufficient_money", "§c金币不足！传送需要 §6{cost}§c 金币。"},
              {"cooldown", "§c传送冷却中！还需等待 §e{seconds}§c 秒。"},
              {"admin_success", "§a已将你传送至箱子位置: {x}, {y}, {z}"}}                                      },
            {"floating",
             {{"locked", "§e[上锁箱子]§r 拥有者: {owner}"},
              {"recycle", "§a[回收商店]§r 拥有者: {owner}"},
              {"shop", "§b[商店箱子]§r 拥有者: {owner}"},
              {"public", "§d[公共箱子]§r 拥有者: {owner}"},
              {"unknown", "§f[未知箱子类型]§r 拥有者: {owner}"},
              {"shop_sell", "§b[商店箱子]§r 出售: {item}"},
              {"recycle_buy", "§a[回收商店]§r 回收: {item}"},
              {"shop_empty", "§b[商店箱子]§r (无物品)"},
              {"recycle_empty", "§a[回收商店]§r (无物品)"},
              {"item_info", "{item} §b[库存: {stock}/{chest_stock}]§r §6[价格: {price}]§r"}}                      },
            {"form",
             {{"shop_items_title", "商品列表"},
              {"shop_set_price_title", "设置物品价格"},
              {"shop_manage_item_title", "管理商品"},
              {"shop_manage_title", "管理商店"},
              {"shop_buy_title", "购买商品"},
              {"shop_set_name_title", "设置商店名称"},
              {"shop_records_title", "购买记录"},
              {"recycle_shop_title", "回收商店"},
              {"recycle_confirm_title", "确认回收"},
              {"recycle_final_title", "最终确认"},
              {"recycle_manage_title", "管理回收商店"},
              {"recycle_manage_content", "你可以添加或查看回收委托。"},
              {"recycle_edit_title", "编辑回收委托"},
              {"recycle_details_title", "回收委托详情"},
              {"recycle_view_title", "查看回收委托"},
              {"recycle_view_empty", "目前没有任何回收委托。"},
              {"recycle_view_content", "点击委托查看详情或进行编辑。"},
              {"recycle_add_title", "添加回收委托"},
              {"recycle_add_content", "从你的背包中选择一个物品进行回收。"},
              {"recycle_set_price_title", "设置回收单价"},
              {"recycle_add_by_id_title", "通过ID添加回收委托"},
              {"share_title", "分享管理"},
              {"share_owner", "箱子主人: {owner}"},
              {"share_list", "分享列表:"},
              {"share_none", "暂无分享玩家"},
              {"share_add_online_title", "添加在线分享玩家 (第 {page} 页)"},
              {"share_add_offline_title", "添加离线分享玩家"},
              {"button_back", "返回"},
              {"button_cancel", "取消"},
              {"button_add_online", "添加在线玩家"},
              {"button_add_offline", "添加离线玩家"},
              {"button_confirm_recycle", "确认回收"},
              {"button_add_commission", "添加回收委托"},
              {"button_view_commission", "查看回收委托"},
              {"button_edit_commission", "编辑委托"},
              {"button_add_by_id", "通过物品ID添加"},
              {"button_set_price", "设置价格"},
              {"button_remove_item", "移除物品"},
              {"button_view_records", "查看记录"},
              {"button_remove_share", "移除分享玩家"},
              {"label_setting_commission", "正在为物品 {item} 设置回收委托"},
              {"label_available_count", "你拥有的数量: {count}"},
              {"label_unit_price", "回收单价: {price}"},
              {"input_recycle_count", "请输入回收数量"},
              {"max_recycle_info", "(该委托最大可回收数量: {max})"},
              {"recycle_final_content", "你确定要回收 {item} x{count} 吗？\n{max_info}\n你将获得 §6{price}§r 金币。"},
              {"label_item", "物品: {item}"},
              {"input_price_label", "回收单价"},
              {"input_max_recycle_label", "最大回收数量 (0为无限制)"},
              {"label_current_aux", "当前物品特殊值: {value}"},
              {"input_price", "回收单价 (金币)"},
              {"input_aux_value", "筛选特殊值 (-1为不筛选)"},
              {"input_min_durability", "最低耐久度要求"},
              {"input_enchants", "要求附魔 (例如 0:1,1:1)"},
              {"input_enchants_example", "§7格式: ID:等级,ID:等级 (如 0:1 表示保护 I)"},
              {"input_enchants_tip", "§7留空则不要求附魔，回收时只要等级不低于要求即可。"},
              {"input_max_recycle", "最大回收数量 (0为无限制)"},
              {"label_item_id_hint", "请输入物品命名空间ID (如 apple 或 diamond_sword)"},
              {"input_item_id", "物品ID"},
              {"label_item_id_tip1", "§7注意: 如果是插件物品，请确保ID正确。"},
              {"label_item_id_tip2", "§7例如: minecraft:apple 或 mypack:custom_item"},
              {"label_setting_price", "正在为物品 {item} 设置价格"},
              {"label_managing_item", "正在管理物品: {item}"},
              {"label_current_price", "当前单价: {price}"},
              {"label_not_priced", "§7未定价"},
              {"label_db_stock", "数据库库存: {count}"},
              {"label_chest_stock", "箱子实际库存: {count}"},
              {"label_buying_item", "正在购买物品: {item}"},
              {"input_buy_count", "购买数量"},
              {"label_your_balance", "你的余额: {balance}"},
              {"input_offline_player", "输入玩家名称"},
              {"data_corrupt_button", "§c数据损坏"},
              {"data_corrupt_button2", "§c数据损坏(2)"},
              {"recycle_price_label", " §6[回收单价: {price}]§r"},
              {"recycle_require_aux", "\n§e要求特殊值: {value}§r"},
              {"recycle_require_durability", "\n§a最低耐久: {value}§r"},
              {"recycle_require_enchant", "\n§d要求附魔: "},
              {"durability_label", "§a耐久: {current} / {max}"},
              {"aux_value_label", "§e特殊值: {value}"},
              {"enchant_label", "§d附魔: "},
              {"set_recycle_name_title", "设置回收商店名称"},
              {"label_item_prefix", "物品: "},
              {"current_recycle_price", "§6当前回收单价: {price}§r\n"},
              {"max_recycle_count_label", "§e最大回收数量: {count}§r\n"},
              {"current_recycled_count_label", "§a已回收数量: {count}§r\n\n"},
              {"max_recycle_unlimited", "§e最大回收数量: 无限制§r\n\n"},
              {"no_recycle_records", "§7该委托暂无回收记录。"},
              {"recent_recycle_records", "§a最近的回收记录:\n"},
              {"recycle_record_format", "§f{timestamp} - {player} 回收了 {count} 个，花费 {price} 金币\n"},
              {"unlimited_label", "§7(无限)"},
              {"price_tag", " §6[单价: {price}]§r"},
              {"page_selection", "--- 分页选择 ---"},
              {"select_page", "选择页码"},
              {"player_selection_tip", "--- 玩家选择 (请在选择页码后提交表单) ---"},
              {"select_online_player", "选择要分享的在线玩家："},
              {"no_online_players", "当前没有其他在线玩家可供分享。"},
              {"remove_share_title", "移除分享玩家 (第 {page} 页)"},
              {"no_shared_players", "当前没有已分享的玩家可供移除。"},
              {"select_remove_player", "选择要移除的分享玩家："},
              {"remaining_stock", "剩余库存: §b{stock}§r"},
              {"purchase_record_format", "§f{timestamp} - {player} 购买了 {item} x{count}，花费 {price} 金币\n"}}}
        };
        std::ofstream out(zhFile);
        out << zh.dump(4);
        logger.info("Created default language file: {}", zhFile);
    }

    // 英文语言文件
    std::string enFile = langDir + "/en_US.json";
    if (!std::filesystem::exists(enFile)) {
        nlohmann::json en = {
            {"chest",
             {{"locked", "§cThis chest is locked, you cannot open it."},
              {"not_owner", "§cOnly the chest owner can perform this action."},
              {"create_success", "§aChest successfully set as {type}! Cost: {price}"},
              {"create_fail", "§cFailed to set {type}! Money refunded"},
              {"remove_success", "§aChest settings removed successfully!"},
              {"remove_fail", "§cFailed to remove chest settings!"},
              {"no_permission", "§cYou don't have permission to create this type of chest!"},
              {"limit_reached", "§cYou have reached the limit of {type} ({max})!"},
              {"config_saved", "§aChest settings saved!"},
              {"config_fail", "§cFailed to save chest settings!"},
              {"entity_fail", "§cFailed to get chest data."},
              {"set_fail_transaction", "§cChest setup failed: Cannot start transaction!"},
              {"set_fail", "§cChest setup failed!"},
              {"set_fail_commit", "§cChest setup failed: Transaction commit failed!"},
              {"set_success", "§aChest setup successful!"},
              {"remove_fail_db", "§cFailed to remove chest settings!"},
              {"unknown_type", "§cUnknown chest type!"},
              {"type_locked", "Locked Chest"},
              {"type_public", "Public Chest"},
              {"type_recycle", "Recycle Shop"},
              {"type_shop", "Shop"},
              {"type_unknown", "Unknown Type"},
              {"name_locked", "'s Locked Chest"},
              {"name_public", "'s Public Chest"}}                                                                },
            {"shop",
             {{"insufficient_money", "§cInsufficient coins! Need §6{price}§c coins."},
              {"insufficient_stock", "§cNot enough items in chest! Actual stock: {stock}"},
              {"purchase_success", "§aPurchase successful! You spent §6{price}§a coins for {item} x{count}."},
              {"purchase_fail", "§cPurchase failed!"},
              {"purchase_chest_fail", "§cPurchase failed, not enough items in chest!"},
              {"purchase_money_fail", "§cPurchase failed, coin deduction failed."},
              {"purchase_db_fail", "§cPurchase failed, database update failed."},
              {"purchase_id_fail", "§cPurchase failed, cannot get item ID."},
              {"item_not_exist", "§cItem does not exist!"},
              {"price_set_success", "§aItem price set successfully! Price: {price}, Count: {count}"},
              {"price_set_fail", "§cFailed to set item price!"},
              {"item_def_fail", "§cFailed to create item definition!"},
              {"item_removed", "§aItem removed successfully!"},
              {"item_remove_fail", "§cFailed to remove item!"},
              {"empty", "The shop is empty, no items available.\n"},
              {"name_set_success", "§aShop name set successfully!"},
              {"name_set_fail", "§cFailed to set shop name!"},
              {"data_corrupt", "§cThis item data is corrupted, cannot purchase."},
              {"item_manage_fail", "§cCannot manage this item, failed to create from NBT."},
              {"item_id_fail", "§cCannot manage this item, failed to get item ID."},
              {"chest_empty", "The chest is empty.\n"},
              {"no_records", "§7No purchase records for this shop."},
              {"records_title", "§aRecent purchase records:\n"},
              {"unknown_item", "Unknown Item"}}                                                                  },
            {"recycle",
             {{"success", "§aSuccessfully recycled {item} x{count}, earned §6{price}§a coins."},
              {"fail", "§cRecycle failed!"},
              {"commission_set", "§aRecycle commission set! Price: {price}, Max count: {max}"},
              {"commission_update", "§aCommission updated! New price: {price}, New max count: {max}"},
              {"no_commission", "§cCannot find this recycle commission."},
              {"no_item_def", "§cCannot find item definition for this commission."},
              {"not_recycle_shop", "§cRecycle failed, this chest is no longer a recycle shop."},
              {"owner_not_found", "§cRecycle failed, shop owner not found."},
              {"owner_insufficient", "§cRecycle failed, shop owner has insufficient balance."},
              {"chest_full", "§cRecycle failed, chest is full. Please clear the chest first."},
              {"max_reached", "§cRecycle failed, commission has reached max count ({max})."},
              {"item_insufficient", "§cRecycle failed, not enough recyclable items in inventory."},
              {"money_refund", "§cRecycle failed, shop owner has insufficient balance. Items returned."},
              {"db_fail", "§cRecycle failed, database update failed."},
              {"chest_entity_fail", "§cRecycle failed, cannot get chest entity."},
              {"item_id_fail", "§cRecycle failed, cannot get item ID."},
              {"data_corrupt", "§cThis commission item data is corrupted, cannot recycle."},
              {"load_fail", "§cFailed to load item info."},
              {"load_records_fail", "§cFailed to load recycle records."},
              {"empty", "This recycle shop has no commissions."},
              {"list_content", "All recycle commissions in this shop:"}}                                         },
            {"economy",
             {{"insufficient", "§cInsufficient money! Need {price}"},
              {"deduct_fail", "§cFailed to deduct money!"},
              {"refund", "Money refunded"}}                                                                      },
            {"action",   {{"cancelled", "§cYou cancelled the action."}}                                         },
            {"share",
             {{"add_success", "§aSuccessfully added player {player} to share list!"},
              {"add_fail", "§cFailed to add shared player!"},
              {"remove_success", "§aSuccessfully removed player {player} from share list!"},
              {"remove_fail", "§cFailed to remove shared player!"},
              {"player_not_found", "§cPlayer {player} not found!"},
              {"name_empty", "§cPlayer name cannot be empty!"}}                                                 },
            {"input",
             {{"invalid_number", "§cInvalid input, please enter a number."},
              {"invalid_count", "§cPurchase count must be greater than 0!"},
              {"invalid_buy_count", "§cInvalid purchase count, please enter a positive integer."},
              {"negative_price", "§cPrice cannot be negative!"},
              {"invalid_recycle_count", "§cInvalid recycle count! Please enter an integer between 1 and {max}."},
              {"negative_durability", "§cMinimum durability cannot be negative!"},
              {"negative_max_count", "§cMax recycle count cannot be negative!"},
              {"invalid_enchant", "§cInvalid enchant format, please use ID:Level,ID:Level format."},
              {"nbt_fail", "§cFailed to get item NBT data."}}                                                   },
            {"teleport",
             {{"success", "§aTeleported to destination! Cost: §6{cost}§a coins."},
              {"insufficient_money", "§cInsufficient coins! Teleport costs §6{cost}§c coins."},
              {"cooldown", "§cTeleport on cooldown! Wait §e{seconds}§c seconds."},
              {"admin_success", "§aTeleported to chest location: {x}, {y}, {z}"}}                               },
            {"floating",
             {{"locked", "§e[Locked Chest]§r Owner: {owner}"},
              {"recycle", "§a[Recycle Shop]§r Owner: {owner}"},
              {"shop", "§b[Shop Chest]§r Owner: {owner}"},
              {"public", "§d[Public Chest]§r Owner: {owner}"},
              {"unknown", "§f[Unknown Type]§r Owner: {owner}"},
              {"shop_sell", "§b[Shop Chest]§r Selling: {item}"},
              {"recycle_buy", "§a[Recycle Shop]§r Buying: {item}"},
              {"shop_empty", "§b[Shop Chest]§r (No items)"},
              {"recycle_empty", "§a[Recycle Shop]§r (No items)"},
              {"item_info", "{item} §b[Stock: {stock}/{chest_stock}]§r §6[Price: {price}]§r"}}               },
            {"form",
             {{"shop_items_title", "Items List"},
              {"shop_set_price_title", "Set Item Price"},
              {"shop_manage_item_title", "Manage Item"},
              {"shop_manage_title", "Manage Shop"},
              {"shop_buy_title", "Buy Item"},
              {"shop_set_name_title", "Set Shop Name"},
              {"shop_records_title", "Purchase Records"},
              {"recycle_shop_title", "Recycle Shop"},
              {"recycle_confirm_title", "Recycle Confirmation"},
              {"recycle_final_title", "Final Confirmation"},
              {"recycle_manage_title", "Manage Recycle Shop"},
              {"recycle_manage_content", "You can add or view recycle commissions."},
              {"recycle_edit_title", "Edit Recycle Commission"},
              {"recycle_details_title", "Recycle Commission Details"},
              {"recycle_view_title", "View Recycle Commissions"},
              {"recycle_view_empty", "There are currently no recycle commissions."},
              {"recycle_view_content", "Click on a commission to view details or edit."},
              {"recycle_add_title", "Add Recycle Commission"},
              {"recycle_add_content", "Select an item from your inventory to recycle."},
              {"recycle_set_price_title", "Set Recycle Price"},
              {"recycle_add_by_id_title", "Add Commission by ID"},
              {"share_title", "Share Management"},
              {"share_owner", "Chest Owner: {owner}"},
              {"share_list", "Share List:"},
              {"share_none", "No shared players"},
              {"share_add_online_title", "Add Online Shared Player (Page {page})"},
              {"share_add_offline_title", "Add Offline Shared Player"},
              {"button_back", "Back"},
              {"button_cancel", "Cancel"},
              {"button_add_online", "Add Online Player"},
              {"button_add_offline", "Add Offline Player"},
              {"button_confirm_recycle", "Confirm Recycle"},
              {"button_add_commission", "Add Recycle Commission"},
              {"button_view_commission", "View Recycle Commissions"},
              {"button_edit_commission", "Edit Commission"},
              {"button_add_by_id", "Add by Item ID"},
              {"button_set_price", "Set Price"},
              {"button_remove_item", "Remove Item"},
              {"button_view_records", "View Records"},
              {"button_remove_share", "Remove Shared Player"},
              {"label_setting_commission", "Setting recycle commission for {item}"},
              {"label_available_count", "Available count: {count}"},
              {"label_unit_price", "Recycle Price: {price}"},
              {"input_recycle_count", "Enter recycle count"},
              {"max_recycle_info", "(Max recycle count for this commission: {max})"},
              {"recycle_final_content",
               "Are you sure you want to recycle {item} x{count}?\n{max_info}\nYou will receive §6{price}§r coins."},
              {"label_item", "Item: {item}"},
              {"input_price_label", "Recycle Price"},
              {"input_max_recycle_label", "Max Recycle Count (0 for unlimited)"},
              {"label_current_aux", "Current Item Aux Value: {value}"},
              {"input_price", "Recycle Price (Coins)"},
              {"input_aux_value", "Filter Aux Value (-1 for no filter)"},
              {"input_min_durability", "Min Durability Requirement"},
              {"input_enchants", "Required Enchants (e.g. 0:1,1:1)"},
              {"input_enchants_example", "§7Format: ID:Level,ID:Level (e.g. 0:1 for Protection I)"},
              {"input_enchants_tip", "§7Leave empty for no enchant requirement."},
              {"input_max_recycle", "Max Recycle Count (0 for unlimited)"},
              {"label_item_id_hint", "Enter item namespace ID (e.g. apple or diamond_sword)"},
              {"input_item_id", "Item ID"},
              {"label_item_id_tip1", "§7Note: Ensure the ID is correct for addon items."},
              {"label_item_id_tip2", "§7Example: minecraft:apple or mypack:custom_item"},
              {"label_setting_price", "Setting price for {item}"},
              {"label_managing_item", "Managing item: {item}"},
              {"label_current_price", "Current Price: {price}"},
              {"label_not_priced", "§7Not Priced"},
              {"label_db_stock", "DB Stock: {count}"},
              {"label_chest_stock", "Chest Stock: {count}"},
              {"label_buying_item", "Buying item: {item}"},
              {"input_buy_count", "Purchase Count"},
              {"label_your_balance", "Your Balance: {balance}"},
              {"input_offline_player", "Enter player name"},
              {"data_corrupt_button", "§cData Corrupt"},
              {"data_corrupt_button2", "§cData Corrupt(2)"},
              {"recycle_price_label", " §6[Recycle Price: {price}]§r"},
              {"recycle_require_aux", "\n§eRequired Aux: {value}§r"},
              {"recycle_require_durability", "\n§aMin Durability: {value}§r"},
              {"recycle_require_enchant", "\n§dRequired Enchants: "},
              {"durability_label", "§aDurability: {current} / {max}"},
              {"aux_value_label", "§eAux Value: {value}"},
              {"enchant_label", "§dEnchants: "},
              {"set_recycle_name_title", "Set Recycle Shop Name"},
              {"label_item_prefix", "Item: "},
              {"current_recycle_price", "§6Current Recycle Price: {price}§r\n"},
              {"max_recycle_count_label", "§eMax Recycle Count: {count}§r\n"},
              {"current_recycled_count_label", "§aRecycled Count: {count}§r\n\n"},
              {"max_recycle_unlimited", "§eMax Recycle Count: Unlimited§r\n\n"},
              {"no_recycle_records", "§7No recycle records for this commission."},
              {"recent_recycle_records", "§aRecent Recycle Records:\n"},
              {"recycle_record_format", "§f{timestamp} - {player} recycled {count} for {price} coins\n"},
              {"unlimited_label", "§7(Unlimited)"},
              {"price_tag", " §6[Price: {price}]§r"},
              {"page_selection", "--- Page Selection ---"},
              {"select_page", "Select Page"},
              {"player_selection_tip", "--- Player Selection (Submit form after selecting page) ---"},
              {"select_online_player", "Select online players to share with:"},
              {"no_online_players", "No other online players available for sharing."},
              {"remove_share_title", "Remove Shared Players (Page {page})"},
              {"no_shared_players", "No shared players available to remove."},
              {"select_remove_player", "Select shared players to remove:"},
              {"remaining_stock", "Remaining Stock: §b{stock}§r"},
              {"purchase_record_format", "§f{timestamp} - {player} bought {item} x{count} for {price} coins\n"}}}
        };
        std::ofstream out(enFile);
        out << en.dump(4);
        logger.info("Created default language file: {}", enFile);
    }
}

} // namespace CT
