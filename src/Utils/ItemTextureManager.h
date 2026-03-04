#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>



namespace CT {

class ItemTextureManager {
private:
    std::unordered_map<std::string, std::vector<std::string>> mItemTextures;
    std::unordered_map<std::string, std::vector<std::string>> mIconKeyTextures;

    ItemTextureManager() = default; // 私有构造函数

public:
    static ItemTextureManager& getInstance();

    bool        loadTextures(const std::string& filePath);
    bool        loadTextures(const std::vector<std::string>& filePaths);
    std::string getTexture(const std::string& itemName, short auxValue = 0) const;
    std::string getTextureByIconKey(const std::string& iconKey, short auxValue = 0) const;

private:
    // 辅助函数：标准化物品名称，使其更接近 item_texture.json 中的键
    static std::string standardizeItemName(std::string name);

    // 辅助函数：解析单个物品的纹理数据，支持嵌套数组
    void parseTextureEntry(const std::string& itemName, const nlohmann::json& texturesValue);
    static std::string extractTextureLeafKey(const std::string& texturePath);
    static void        appendUnique(std::vector<std::string>& list, const std::string& value);
    void               addTextureMapping(const std::string& itemName, const std::string& texturePath);
    std::string        pickTextureByAux(const std::vector<std::string>& textures, short auxValue) const;
};

} // namespace CT
