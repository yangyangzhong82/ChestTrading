#include "mc/legacy/ActorUniqueID.h"

class Vec3;
class Player;
class BlockSource;
class ItemStack;

ActorUniqueID AddFakeitem(Vec3 pos, Player& player, BlockSource& region, ItemStack& item);
void RemoveFakeitem(Player& player, ActorUniqueID id);
