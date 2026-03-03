#pragma once

#include <string>

#if CT_ENABLE_PERMISSION_GROUP
#include "Bedrock-Authority/permission/PermissionManager.h"
#endif

namespace CT::PermissionCompat {

inline bool hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
#if CT_ENABLE_PERMISSION_GROUP
    return BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, permissionNode);
#else
    (void)playerUuid;
    (void)permissionNode;
    return true;
#endif
}

} // namespace CT::PermissionCompat
