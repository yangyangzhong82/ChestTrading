#pragma once

#include "ll/api/event/player/PlayerPlaceBlockEvent.h"

namespace CT {
void handlePlayerPlacingBlock(ll::event::PlayerPlacingBlockEvent& ev);
void handlePlayerPlacedBlock(ll::event::PlayerPlacedBlockEvent& ev);
} // namespace CT
