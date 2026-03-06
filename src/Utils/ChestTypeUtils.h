#pragma once

#include "mc/world/level/block/Block.h"

#include <string>
#include <string_view>

namespace CT::ChestTypeUtils {

inline bool isCopperChestTypeName(std::string_view typeName) {
    return typeName.find("copper_chest") != std::string_view::npos;
}

inline bool isSupportedChestTypeName(std::string_view typeName) {
    return typeName == "minecraft:chest" || isCopperChestTypeName(typeName);
}

inline bool isSupportedChestBlock(const Block& block) { return isSupportedChestTypeName(block.getTypeName()); }

inline bool isSupportedChestItemTypeName(const std::string& typeName) { return isSupportedChestTypeName(typeName); }

} // namespace CT::ChestTypeUtils
