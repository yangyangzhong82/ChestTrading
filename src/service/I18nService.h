#pragma once

#include <map>
#include <string>

namespace CT {

class I18nService {
public:
    static I18nService& getInstance();

    I18nService(const I18nService&)            = delete;
    I18nService& operator=(const I18nService&) = delete;

    // 加载语言文件，langDir 为语言文件目录，lang 为语言代码如 "zh_CN"
    bool load(const std::string& langDir, const std::string& lang);

    // 获取翻译文本
    std::string get(const std::string& key, const std::map<std::string, std::string>& params = {}) const;

    // 获取当前语言
    const std::string& getCurrentLang() const { return mCurrentLang; }

private:
    I18nService() = default;

    std::string replacePlaceholders(const std::string& text, const std::map<std::string, std::string>& params) const;
    void        createDefaultLangFiles(const std::string& langDir);

    std::map<std::string, std::string> mMessages;
    std::string                        mCurrentLang;
    std::string                        mLangDir;
};

} // namespace CT
