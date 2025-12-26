#include "ItemTextureManager.h"
#include "logger.h" // 假设有logger.h用于日志输出
#include <fstream>

namespace CT {

ItemTextureManager& ItemTextureManager::getInstance() {
    static ItemTextureManager instance;
    return instance;
}

bool ItemTextureManager::loadTextures(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        logger.error("无法打开物品贴图文件: {}", filePath);
        return false;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ifs, nullptr, true, true); // 允许注释
    } catch (const nlohmann::json::parse_error& e) {
        logger.error("解析物品贴图文件失败: {} - {}", filePath, e.what());
        return false;
    }

    // 尝试解析扁平的 JSON 格式 (例如 texture_path.json)
    bool isFlatFormat = true;
    if (j.is_object()) {
        for (auto const& [itemName, texturePath] : j.items()) {
            if (!texturePath.is_string()) {
                isFlatFormat = false; // 如果有非字符串值，则不是扁平格式
                break;
            }
        }
    } else {
        isFlatFormat = false;
    }

    if (isFlatFormat) {
        for (auto const& [itemName, texturePath] : j.items()) {
            mItemTextures[itemName].push_back(texturePath.get<std::string>());
        }
        logger.info("成功加载扁平格式物品贴图文件: {}，共 {} 个贴图。", filePath, mItemTextures.size());
        return true;
    }
    // 如果不是扁平格式，或者扁平格式解析失败，则尝试解析旧的 texture_data 格式
    else if (j.contains("texture_data") && j["texture_data"].is_object()) {
        for (auto const& [itemName, itemData] : j["texture_data"].items()) {
            if (itemData.is_object()) {
                if (itemData.contains("textures")) {
                    parseTextureEntry(itemName, itemData["textures"]);
                } else if (itemData.contains("path")) { // 处理直接包含 "path" 字段的情况
                    parseTextureEntry(itemName, itemData);
                }
            }
        }
        logger.info("成功加载旧格式物品贴图文件: {}，共 {} 个贴图。", filePath, mItemTextures.size());
        return true;
    } else {
        logger.error("物品贴图文件 {} 格式不正确，既不是扁平格式，也缺少 'texture_data' 字段。", filePath);
        return false;
    }
}

// 按顺序加载多个贴图文件，先加载的文件优先级更高（已存在的贴图不会被覆盖）
bool ItemTextureManager::loadTextures(const std::vector<std::string>& filePaths) {
    bool all_succeeded = true;
    for (const auto& filePath : filePaths) {
        if (!loadTextures(filePath)) {
            all_succeeded = false;
        }
    }
    return all_succeeded;
}

// 辅助函数：解析单个物品的纹理数据，支持嵌套数组
void ItemTextureManager::parseTextureEntry(const std::string& itemName, const nlohmann::json& texturesValue) {
    if (texturesValue.is_string()) {
        mItemTextures[itemName].push_back(texturesValue.get<std::string>());
    } else if (texturesValue.is_array()) {
        for (const auto& element : texturesValue) {
            // 递归处理嵌套数组或对象
            parseTextureEntry(itemName, element);
        }
    } else if (texturesValue.is_object() && texturesValue.contains("path") && texturesValue["path"].is_string()) {
        // 处理包含 "path" 字段的对象
        mItemTextures[itemName].push_back(texturesValue["path"].get<std::string>());
    }
}

// 辅助函数：标准化物品名称，使其更接近 item_texture.json 中的键
std::string ItemTextureManager::standardizeItemName(std::string name) {
    // 移除 "minecraft:" 前缀 (已经在 LockForm.cpp 中处理，但为了健壮性，这里也可以再处理一次)
    if (name.rfind("minecraft:", 0) == 0) {
        name = name.substr(10);
    }

    // 转换 cooked_xxx 为 xxx_cooked
    if (name.rfind("cooked_", 0) == 0) {
        std::string base = name.substr(7);
        if (base == "porkchop") return "porkchop_cooked";
        if (base == "beef") return "beef_cooked";
        if (base == "chicken") return "chicken_cooked";
        if (base == "mutton") return "mutton_cooked";
        // 对于 fish 和 salmon，json 中是 cooked_fish 和 cooked_salmon，所以不需要转换
    }

    // 移除 _item 后缀 (例如 lodestonecompass_item -> lodestonecompass)
    if (name.length() > 5 && name.substr(name.length() - 5) == "_item") {
        name = name.substr(0, name.length() - 5);
    }

    // 更多标准化规则可以根据需要添加
    return name;
}

std::string ItemTextureManager::getTexture(const std::string& rawItemName) const {
    std::string itemName = standardizeItemName(rawItemName);

    // 1. 尝试完全匹配标准化后的名称
    auto it = mItemTextures.find(itemName);
    if (it != mItemTextures.end() && !it->second.empty()) {
        return it->second[0];
    }

    // 2. 尝试通用类型匹配 (例如 "iron_sword" 匹配 "sword")
    // 提取材质前缀 (例如 "iron", "diamond", "wood", "stone", "gold", "netherite", "leather", "chainmail")
    std::vector<std::string> materials =
        {"iron", "diamond", "wood", "stone", "gold", "netherite", "leather", "chainmail"};
    std::string foundMaterial;
    std::string baseItemName = itemName;

    for (const auto& material : materials) {
        if (itemName.rfind(material + "_", 0) == 0) { // 检查是否以材质开头
            foundMaterial = material;
            baseItemName  = itemName.substr(material.length() + 1); // 移除材质前缀
            break;
        }
    }

    // 如果找到了材质，并且 baseItemName 存在于 mItemTextures 中
    if (!foundMaterial.empty()) {
        it = mItemTextures.find(baseItemName);
        if (it != mItemTextures.end() && !it->second.empty()) {
            // 在通用类型的贴图数组中查找包含材质的贴图
            for (const auto& texture : it->second) {
                if (texture.find(foundMaterial) != std::string::npos) {
                    return texture;
                }
            }
            return it->second[0]; // 如果找不到特定材质的，返回第一个
        }
    }

    // 3. 尝试更通用的部分匹配 (例如 "bow" 匹配 "bow_standby")
    // 这种情况下，如果 itemName 是 "bow"，而 json 中有 "bow_standby"，我们可能想返回 "bow_standby" 的贴图
    // 但这需要更复杂的逻辑来决定返回哪个，暂时先返回第一个找到的
    for (const auto& [key, textures] : mItemTextures) {
        if (itemName.find(key) != std::string::npos && !textures.empty()) {
            return textures[0];
        }
    }

    return ""; // 未找到贴图
}

} // namespace CT
