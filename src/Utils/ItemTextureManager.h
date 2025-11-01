#pragma once

#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace CT {

class ItemTextureManager {
private:
    std::map<std::string, std::vector<std::string>> mItemTextures;

    ItemTextureManager() = default; // 私有构造函数

public:
    static ItemTextureManager& getInstance(); // 单例模式

    bool loadTextures(const std::string& filePath);
    bool loadTextures(const std::vector<std::string>& filePaths); // 新增：支持加载多个文件
    std::string getTexture(const std::string& itemName) const;

private:
    // 辅助函数：标准化物品名称，使其更接近 item_texture.json 中的键
    static std::string standardizeItemName(std::string name);

    // 辅助函数：解析单个物品的纹理数据，支持嵌套数组
    void parseTextureEntry(const std::string& itemName, const nlohmann::json& texturesValue);
};

} // namespace CT
