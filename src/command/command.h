#include <string>

namespace CT {
void registerCommand();
bool isInPackChestMode(const std::string& uuid);
void setPackChestMode(const std::string& uuid, bool enabled);
} // namespace CT