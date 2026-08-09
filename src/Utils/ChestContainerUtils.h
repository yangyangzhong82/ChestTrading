#pragma once

class BlockPos;
class BlockSource;
class Container;

namespace CT::ChestContainerUtils {

// Returns the merged container for a valid chest, or nullptr while its block/pair is unavailable.
Container* tryGetChestContainer(BlockSource& region, BlockPos const& pos);

} // namespace CT::ChestContainerUtils
