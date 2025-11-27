class Vec3;
class Player;
class BlockSource;
class ItemStack;
class ActorUniqueID;
class AddItemActorPacket ;
AddItemActorPacket AddFakeitem(Vec3 pos, Player& player, BlockSource& region, ItemStack& item);
void RemoveFakeitem(Player& player, ActorUniqueID id);