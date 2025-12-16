#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/NetworkItemStackDescriptor.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc\network\packet\AddItemActorPacket.h"
#include "mc\network\packet\RemoveActorPacket.h"

NetworkItemStackDescriptor::NetworkItemStackDescriptor()
: mIncludeNetIds(false),
  mNetIdVariant(0),
  mBlockRuntimeId(0),
  mUserDataBuffer({}) {};
ItemDescriptorCount::ItemDescriptorCount() : mStackSize(0) {};
ItemDescriptor::ItemDescriptor() = default;

ActorUniqueID AddFakeitem(Vec3 pos, Player& player, BlockSource& region, ItemStack& item) {
    auto  p     = AddItemActorPacket();
    auto  level = ll::service::getLevel();
    auto& spaw  = level->getSpawner();
    auto  is    = spaw.spawnItem(region, item, nullptr, pos, 0);
    if (!is) {
        return ActorUniqueID{};
    }

    auto d                                    = NetworkItemStackDescriptor{item};
    p.mData                                   = {};
    SynchedActorDataEntityWrapper& entityData = is->mEntityData;
    p.mEntityData                             = &entityData;
    auto id                                   = level->getNewUniqueID();
    p.mId                                     = id;
    p.mRuntimeId                              = level->getNextRuntimeID();
    p.mItem                                   = d;
    p.mVelocity                               = {0, 0, 0};
    p.mPos                                    = pos;
    p.mIsFromFishing                          = false;
    player.sendNetworkPacket(p);
    is->despawn();
    return id;
}

void RemoveFakeitem(Player& player, ActorUniqueID id) {
    auto r      = RemoveActorPacket();
    r.mEntityId = id;
    player.sendNetworkPacket(r);
}
