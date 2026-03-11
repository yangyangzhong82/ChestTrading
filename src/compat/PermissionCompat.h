#pragma once

#include <string>

namespace CT::PermissionCompat {

bool hasPermission(const std::string& playerUuid, const std::string& permissionNode);

} // namespace CT::PermissionCompat
