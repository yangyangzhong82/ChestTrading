#include "Event.h"

#include "interaction/ChestDestroyHandler.h"
#include "interaction/ChestInteractHandler.h"
#include "interaction/ChestPlaceHandler.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"

namespace CT {

void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
        [](ll::event::PlayerInteractBlockEvent& ev) { handlePlayerInteractBlock(ev); },
        ll::event::EventPriority::High
    );

    eventBus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& event) {
        handlePlayerDestroyBlock(event);
    });

    eventBus.emplaceListener<ll::event::PlayerPlacingBlockEvent>([](ll::event::PlayerPlacingBlockEvent& ev) {
        handlePlayerPlacingBlock(ev);
    });

    eventBus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
        handlePlayerPlacedBlock(ev);
    });
}

} // namespace CT
