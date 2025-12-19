#include "Event.h"

#include "interaction/ChestDestroyHandler.h"
#include "interaction/ChestInteractHandler.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

namespace CT {

void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
        handlePlayerInteractBlock(ev);
    });

    eventBus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& event) {
        handlePlayerDestroyBlock(event);
    });
}

} // namespace CT
