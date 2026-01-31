#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/AddItemActorPacket.h"
#include "mc/network/packet/RemoveActorPacket.h"

ActorUniqueID AddFakeitem(Vec3 pos, Player& player, BlockSource& region, ItemStack& item) {
    auto level = ll::service::getLevel();
    if (!level) {
        return ActorUniqueID{};
    }

    // Build a real (but temporary) ItemActor so AddItemActorPacket has valid actor data to serialize.
    // This avoids crashes from partially/incorrectly initialized packet fields.
    auto& actorFactory = level->getActorFactory();
    auto  ctx          = actorFactory._constructActor(ActorDefinitionIdentifier(ActorType::ItemEntity, ""), pos, Vec2{0, 0}, nullptr);
    if (!ctx) {
        return ActorUniqueID{};
    }

    auto* actor = Actor::tryGetFromEntity(*ctx, true);
    if (!actor || actor->getEntityTypeId() != ActorType::ItemEntity) {
        return ActorUniqueID{};
    }

    auto& itemActor = static_cast<ItemActor&>(*actor);
    (void)region;
    itemActor.item()         = item;
    itemActor.isFromFishing() = false;

    AddItemActorPacket packet(itemActor);

    // Use a controlled id so the follow-up RemoveActorPacket can reliably match it.
    // (We cannot mutate the temporary actor's ids on server builds, so we patch the packet fields directly.)
    auto rid = level->getNextRuntimeID();
    auto uid = ActorUniqueID{static_cast<int64>(rid.rawID)};
    packet.mRuntimeId = rid;
    packet.mId        = uid;

    auto id = uid;
    player.sendNetworkPacket(packet);
    return id;
}

void RemoveFakeitem(Player& player, ActorUniqueID id) {
    auto packetBase = MinecraftPackets::createPacket(MinecraftPacketIds::RemoveActor);
    if (!packetBase || packetBase->getId() != MinecraftPacketIds::RemoveActor) {
        return;
    }

    auto* packet = static_cast<RemoveActorPacket*>(packetBase.get());
    packet->mEntityId = id;
    player.sendNetworkPacket(*packet);
}
