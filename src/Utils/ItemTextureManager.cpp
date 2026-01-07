#include "ItemTextureManager.h"
#include "logger.h"
#include <fstream>

namespace CT {

ItemTextureManager& ItemTextureManager::getInstance() {
    static ItemTextureManager instance;
    return instance;
}

bool ItemTextureManager::loadTextures(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return (logger.error("无法打开物品贴图文件: {}", filePath), false);

    try {
        nlohmann::json j    = nlohmann::json::parse(ifs, nullptr, true, true);
        const auto&    data = j.contains("texture_data") ? j["texture_data"] : j;
        if (data.is_object()) {
            for (auto const& [itemName, itemData] : data.items()) {
                parseTextureEntry(
                    itemName,
                    itemData.is_object() && itemData.contains("textures") ? itemData["textures"] : itemData
                );
            }
            logger.info("成功加载贴图文件: {}，项目数: {}", filePath, mItemTextures.size());
            return true;
        }
    } catch (const std::exception& e) {
        logger.error("解析物品贴图文件失败: {} - {}", filePath, e.what());
    }
    return false;
}

bool ItemTextureManager::loadTextures(const std::vector<std::string>& filePaths) {
    bool ok = true;
    for (const auto& path : filePaths) ok &= loadTextures(path);
    return ok;
}

void ItemTextureManager::parseTextureEntry(const std::string& itemName, const nlohmann::json& val) {
    if (val.is_string()) mItemTextures[itemName].push_back(val.get<std::string>());
    else if (val.is_array())
        for (const auto& e : val) parseTextureEntry(itemName, e);
    else if (val.is_object() && val.contains("path") && val["path"].is_string())
        mItemTextures[itemName].push_back(val["path"].get<std::string>());
}

std::string ItemTextureManager::standardizeItemName(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) name = name.substr(10);
    if (name.rfind("waxed_", 0) == 0) name = name.substr(6);
    if (name.length() > 5 && name.substr(name.length() - 5) == "_item") name = name.substr(0, name.length() - 5);

    if (name == "tropical_fish_bucket") name = "tropical_bucket";
    if (name == "fish_bucket") name = "cod_bucket";
    if (name.rfind("music_disc_", 0) == 0) name = "record_" + name.substr(11);

    if (name.size() > 10 && name.substr(name.size() - 10) == "_spawn_egg") {
        name = "spawn_egg_" + name.substr(0, name.size() - 10);
    }

    auto check = [&](const std::string& s) {
        return name.size() > s.size() && name.substr(name.size() - s.size()) == s;
    };

    static const std::vector<std::string> reversePrefixes = {"cooked_", "baked_", "raw_", "enchanted_", "golden_"};
    for (const auto& pre : reversePrefixes) {
        if (name.rfind(pre, 0) == 0) {
            name = name.substr(pre.length()) + "_" + pre.substr(0, pre.length() - 1);
            break;
        }
    }

    if (name.length() > 7 && name.substr(name.length() - 7) == "_golden") {
        if (name != "apple_golden" && name != "carrot_golden") {
            name = "gold_" + name.substr(0, name.size() - 7);
        }
    }
    if (name.rfind("wooden_", 0) == 0) {
        name = (name == "wooden_door") ? "wooden_door" : "wood_" + name.substr(7);
    }

    static const std::vector<std::string> woods =
        {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "crimson", "warped", "pale_oak"
        };
    for (const auto& w : woods) {
        if (name.rfind(w + "_", 0) == 0) {
            if (check("_fence") || check("_fence_gate") || check("_stairs") || check("_button")
                || check("_pressure_plate") || check("_slab")) {
                if (w == "bamboo" || w == "nether_brick") break;
                return w + "_planks";
            }
        }
    }
    if (check("_wall")) name = name.substr(0, name.size() - 5);

    static const std::vector<std::string> colors = {
        "white",
        "orange",
        "magenta",
        "light_blue",
        "yellow",
        "lime",
        "pink",
        "gray",
        "light_gray",
        "cyan",
        "purple",
        "blue",
        "brown",
        "green",
        "red",
        "black"
    };
    for (const auto& c : colors) {
        if (name == c + "_wool" || name == c + "_carpet") return "wool_colored_" + (c == "light_gray" ? "silver" : c);
    }

    static const std::unordered_map<std::string, std::string> map = {
        {"arrow",                    "arrow"                   },
        {"tipped_arrow",             "tipped_arrow"            },
        {"fence",                    "oak_planks"              },
        {"fence_gate",               "oak_planks"              },
        {"hay_block",                "hayblock_side"           },
        {"honey_block",              "honey_side"              },
        {"bee_nest",                 "bee_nest_front"          },
        {"beehive",                  "beehive_front"           },
        {"slime_ball",               "slimeball"               },
        {"magma_cream",              "magma_cream"             },
        {"totem_of_undying",         "totem"                   },
        {"turtle_scute",             "turtle_shell_piece"      },
        {"nether_brick",             "netherbrick"             },
        {"fire_charge",              "fireball"                },
        {"sugar_cane",               "reeds"                   },
        {"redstone",                 "redstone_dust"           },
        {"lapis_lazuli",             "dye_powder"              },
        {"ink_sac",                  "dye_powder"              },
        {"cocoa_beans",              "dye_powder"              },
        {"bone_meal",                "dye_powder"              },
        {"glass_bottle",             "potion_bottle_empty"     },
        {"potion",                   "potion_bottle_drinkable" },
        {"splash_potion",            "potion_bottle_splash"    },
        {"lingering_potion",         "potion_bottle_lingering" },
        {"map",                      "map_empty"               },
        {"empty_map",                "map_empty"               },
        {"filled_map",               "map_filled"              },
        {"explorer_map",             "map_filled"              },
        {"book",                     "book_normal"             },
        {"enchanted_book",           "book_enchanted"          },
        {"writable_book",            "book_writable"           },
        {"written_book",             "book_written"            },
        {"minecart",                 "minecart_normal"         },
        {"chest_minecart",           "minecart_chest"          },
        {"hopper_minecart",          "minecart_hopper"         },
        {"tnt_minecart",             "minecart_tnt"            },
        {"furnace_minecart",         "minecart_furnace"        },
        {"command_block_minecart",   "minecart_command_block"  },
        {"ender_pearl",              "ender_pearl"             },
        {"eye_of_ender",             "ender_eye"               },
        {"clock",                    "clock_item"              },
        {"compass",                  "compass_item"            },
        {"lodestone_compass",        "lodestonecompass_item"   },
        {"white_dye",                "dye_powder"              },
        {"orange_dye",               "dye_powder"              },
        {"magenta_dye",              "dye_powder"              },
        {"light_blue_dye",           "dye_powder"              },
        {"yellow_dye",               "dye_powder"              },
        {"lime_dye",                 "dye_powder"              },
        {"pink_dye",                 "dye_powder"              },
        {"gray_dye",                 "dye_powder"              },
        {"light_gray_dye",           "dye_powder"              },
        {"cyan_dye",                 "dye_powder"              },
        {"purple_dye",               "dye_powder"              },
        {"blue_dye",                 "dye_powder"              },
        {"brown_dye",                "dye_powder"              },
        {"green_dye",                "dye_powder"              },
        {"red_dye",                  "dye_powder"              },
        {"black_dye",                "dye_powder"              },
        {"glow_ink_sac",             "dye_powder"              },
        {"mooshroom_spawn_egg",      "spawn_egg_mooshroom"     },
        {"polar_bear_spawn_egg",     "spawn_egg_polar_bear"    },
        {"skeleton_horse_spawn_egg", "spawn_egg_skeleton_horse"},
        {"zombie_horse_spawn_egg",   "spawn_egg_zombie_horse"  },
        {"elder_guardian_spawn_egg", "spawn_egg_elder_guardian"}
    };
    if (auto it = map.find(name); it != map.end()) return it->second;

    if (check("_slab")) {
        std::string                                               b    = name.substr(0, name.size() - 5);
        static const std::unordered_map<std::string, std::string> sMap = {
            {"stone",             "smooth_stone"               },
            {"smooth_stone",      "smooth_stone"               },
            {"sandstone",         "flattened_sandstone"        },
            {"quartz",            "flattened_quartz_block_side"},
            {"red_sandstone",     "flattened_redsandstone"     },
            {"purpur",            "flattened_purpur_block"     },
            {"prismarine",        "flattened_prismarine"       },
            {"mossy_cobblestone", "cobblestone_mossy"          },
            {"end_stone_brick",   "end_bricks"                 }
        };
        if (auto it = sMap.find(b); it != sMap.end()) return it->second;
    }
    return name;
}

std::string ItemTextureManager::getTexture(const std::string& rawItemName, short aux) const {
    std::string name = standardizeItemName(rawItemName);
    auto        it   = mItemTextures.find(name);

    // 优先查找独立的 spawn_egg_xxx 条目（新路径格式）
    if (name.rfind("spawn_egg_", 0) == 0 && it != mItemTextures.end() && !it->second.empty()) {
        return it->second[0];
    }

    if (it != mItemTextures.end() && !it->second.empty()) {
        size_t idx = 0;
        if (name.find("potion") != std::string::npos || name == "tipped_arrow" || (name == "arrow" && aux > 0)) {
            // 如果是普通的 arrow 且 aux > 0，在 BE 中它实际上是 tipped_arrow
            std::string textureKey = name;
            if (name == "arrow" && aux > 0) {
                textureKey = "tipped_arrow";
                auto tit   = mItemTextures.find(textureKey);
                if (tit != mItemTextures.end()) it = tit;
            }

            static const std::unordered_map<short, int> pMap = {
                {0,  0 }, // 水瓶 / 药箭
                {1,  1 }, // 平凡 (药水)
                {2,  2 }, // 药箭 (平凡) / 延长 (药水)
                {3,  2 }, // 延长 (平凡药箭) / 浓稠 (药水)
                {4,  2 }, // 浓稠 (药箭) / 粗制 (药水)
                {5,  2 }, // 粗制 (药箭) / 夜视 (药水)
                {6,  11}, // 夜视 (药箭) / 延长 (夜视药水)
                {7,  11}, // 延长 (夜视药箭) / 隐身 (药水)
                {8,  10}, // 隐身 (药箭) / 延长 (隐身药水)
                {9,  10}, // 延长 (隐身药箭) / 跳跃 (药水)
                {10, 6 }, // 跳跃 (药箭) / 延长 (跳跃药水)
                {11, 6 }, // 延长 (跳跃药箭) / 加强 (跳跃药水)
                {12, 6 }, // 加强 (跳跃药箭) / 抗火 (药水)
                {13, 8 }, // 抗火 (药箭) / 延长 (抗火药水)
                {14, 8 }, // 延长 (抗火药箭) / 迅捷 (药水)
                {15, 1 }, // 迅捷 (药箭) / 延长 (迅捷药水)
                {16, 1 }, // 延长 (迅捷药箭) / 加强 (迅捷药水)
                {17, 1 }, // 加强 (迅捷药箭) / 迟缓 (药水)
                {18, 2 }, // 迟缓 (药箭) / 延长 (迟缓药水)
                {19, 2 }, // 延长 (迟缓药箭) / 水肺 (药水)
                {20, 9 }, // 水肺 (药箭) / 延长 (水肺药水)
                {21, 9 }, // 延长 (水肺药箭) / 治疗 (药水)
                {22, 4 }, // 治疗 (药箭) / 加强 (治疗药水)
                {23, 4 }, // 加强 (治疗药箭) / 伤害 (药水)
                {24, 5 }, // 伤害 (药箭) / 加强 (伤害药水)
                {25, 5 }, // 加强 (伤害药箭) / 剧毒 (药水)
                {26, 13}, // 剧毒 (药箭) / 延长 (剧毒药水)
                {27, 13}, // 延长 (剧毒药箭) / 加强 (剧毒药水)
                {28, 13}, // 加强 (剧毒药箭) / 再生 (药水)
                {29, 7 }, // 再生 (药箭) / 延长 (再生药水)
                {30, 7 }, // 延长 (再生药箭) / 加强 (再生药水)
                {31, 7 }, // 加强 (再生药箭) / 力量 (药水)
                {32, 3 }, // 力量 (药箭) / 延长 (力量药水)
                {33, 3 }, // 延长 (力量药箭) / 加强 (力量药水)
                {34, 3 }, // 加强 (力量药箭) / 虚弱 (药水)
                {35, 12}, // 虚弱 (药箭) / 延长 (虚弱药水)
                {36, 12}, // 延长 (虚弱药箭) / 衰变 (药水)
                {37, 14}, // 衰变 (药箭) / 神龟 (药水)
                {38, 15}, // 神龟 (药箭) / 延长 (神龟药水)
                {39, 15}, // 延长 (神龟药箭) / 加强 (神龟药水)
                {40, 15}, // 加强 (神龟药箭) / 缓降 (药水)
                {41, 16}, // 缓降 (药箭) / 延长 (缓降药水)
                {42, 16}, // 延长 (缓降药箭) / 加强 (迅捷药水-其实是迟缓加强)
                {43, 2 }, // 加强迟缓 (药箭) / 蓄风 (药水)
                {44, 17}, // 蓄风 (药箭) / 盘丝 (药水)
                {45, 18}, // 盘丝 (药箭) / 渗浆 (药水)
                {46, 19}, // 渗浆 (药箭) / 虫蚀 (药水)
                {47, 20}  // 虫蚀 (药箭)
            };
            if (auto pit = pMap.find(aux); pit != pMap.end()) {
                idx = pit->second;
                if (textureKey != "tipped_arrow") {
                    static const int off[] = {0,  1,  2,  5,  6,  7,  8,  10, 12, 13, 14,
                                              16, 18, 19, 20, 25, 26, 27, 28, 29, 30};
                    if (idx < (int)(sizeof(off) / sizeof(off[0]))) idx = off[idx];
                }
            }
        } else idx = static_cast<size_t>(aux);
        return it->second[idx < it->second.size() ? idx : 0];
    }

    for (const char* s : {"_side", "_top", "_front", ""}) {
        if (auto it = mItemTextures.find(name + s); it != mItemTextures.end() && !it->second.empty())
            return it->second[0];
    }

    static const std::vector<std::string> mats = {
        "iron",   "diamond", "wood",       "stone",    "gold",    "netherite", "leather",    "chainmail",
        "oak",    "spruce",  "birch",      "jungle",   "acacia",  "dark_oak",  "mangrove",   "cherry",
        "bamboo", "crimson", "warped",     "pale_oak", "copper",  "water",     "lava",       "milk",
        "cod",    "salmon",  "pufferfish", "tropical", "axolotl", "tadpole",   "powder_snow"
    };
    for (const auto& m : mats) {
        if (name.rfind(m + "_", 0) == 0) {
            std::string base = name.substr(m.length() + 1);
            for (const char* s : {"", "_side"}) {
                if (auto it = mItemTextures.find(base + s); it != mItemTextures.end() && !it->second.empty()) {
                    for (const auto& t : it->second) {
                        if (t.find(m) != std::string::npos) return t;
                        if (m == "gold" && t.find("golden") != std::string::npos) return t;
                        if (m == "wood" && t.find("wooden") != std::string::npos) return t;
                    }
                    return it->second[0];
                }
            }
        }
    }

    for (auto const& [k, v] : mItemTextures) {
        if (!v.empty() && (k.rfind(name + "_", 0) == 0 || name.rfind(k + "_", 0) == 0)) return v[0];
    }
    return "";
}

} // namespace CT
