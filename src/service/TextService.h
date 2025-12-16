#pragma once

#include "FloatingText/FloatingText.h"
#include <map>
#include <string>

namespace CT {

/**
 * @brief 文本生成服务
 * 统一管理所有UI文本和消息
 */
class TextService {
public:
    static TextService& getInstance();

    TextService(const TextService&)            = delete;
    TextService& operator=(const TextService&) = delete;

    // === 悬浮字文本 ===
    std::string generateChestText(ChestType type, const std::string& ownerName);
    std::string generateShopItemText(const std::string& itemName, double price, int stock, int chestStock);

    // === 消息模板 ===
    std::string getMessage(const std::string& key, const std::map<std::string, std::string>& params = {});

    // === 箱子类型名称 ===
    std::string getChestTypeName(ChestType type);

private:
    TextService();

    std::map<std::string, std::string> mMessages;

    void        initMessages();
    std::string replacePlaceholders(const std::string& text, const std::map<std::string, std::string>& params);
};

} // namespace CT