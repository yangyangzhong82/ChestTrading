#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include <string>

namespace CT {

struct OfficialShopImportResult {
    bool        success       = false;
    int         importedCount = 0;
    int         skippedCount  = 0;
    std::string message;
};

class OfficialShopImportService {
public:
    static OfficialShopImportService& getInstance();

    OfficialShopImportService(const OfficialShopImportService&)            = delete;
    OfficialShopImportService& operator=(const OfficialShopImportService&) = delete;

    OfficialShopImportResult
    importPurchaseItems(Player& player, BlockPos pos, int dimId, const std::string& filePath, bool replaceExisting);

private:
    OfficialShopImportService() = default;
};

} // namespace CT
