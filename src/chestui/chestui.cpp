#include "chestui/chestui.h"

#include "Utils/NetworkPacket.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "logger.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/nbt/ListTag.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ItemStackRequestAction.h"
#include "mc/world/inventory/network/ItemStackRequestActionHandler.h"
#include "mc/world/inventory/network/ItemStackRequestActionTransferBase.h"
#include "mc/world/inventory/network/ItemStackRequestHandlerSlotInfo.h"
#include "mc/world/inventory/network/ItemStackRequestSlotInfo.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"


#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CT::ChestUI {

namespace {

constexpr size_t kChestSlotCount = 27;

struct Session {
    ContainerID                           containerId{ContainerID::None};
    uint                                  dynamicContainerId{0};
    uint64                                generation{0};
    std::chrono::steady_clock::time_point openedAt{};
    uint8                                 reopenAttempts{0};
    BlockPos                              fakePos{};
    uint                                  originalRuntimeId{0};
    std::string                           title;
    std::vector<ItemStack>                items;
    ClickCallback                         onClick;
    CloseCallback                         onClose;
    bool                                  closeOnClick{true};
};

std::mutex                               gSessionMutex;
std::unordered_map<std::string, Session> gSessions;
std::atomic<uint64>                      gSessionGeneration{0};

auto getPlayerKey(Player const& player) -> std::string {
    auto key = player.getXuid();
    if (!key.empty()) {
        return key;
    }
    return player.getUuid().asString();
}

auto findPlayerByNetId(NetworkIdentifier const& source) -> Player* {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }
    Player* result = nullptr;
    level->forEachPlayer([&](Player& player) {
        if (player.getNetworkIdentifier() == source) {
            result = &player;
            return false;
        }
        return true;
    });
    return result;
}

auto findPlayerByIdentity(std::string const& uuidString, std::string const& xuid) -> Player* {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }

    if (!uuidString.empty()) {
        auto uuid = mce::UUID::fromString(uuidString);
        if (uuid != mce::UUID::EMPTY()) {
            if (auto* player = level->getPlayer(uuid)) {
                return player;
            }
        }
    }

    if (xuid.empty()) {
        return nullptr;
    }

    Player* result = nullptr;
    level->forEachPlayer([&](Player& player) {
        if (player.getXuid() == xuid) {
            result = &player;
            return false;
        }
        return true;
    });
    return result;
}

auto makeFakePos(Player const& player, uint64 generation) -> BlockPos {
    auto const pos = player.getPosition();
    auto const bx  = static_cast<int>(pos.x);
    auto const by  = static_cast<int>(pos.y) + 2;
    auto const bz  = static_cast<int>(pos.z);
    auto const sx  = static_cast<int>((generation & 0x3u) * 2u) - 2;         // -2,0,2,4
    auto const sz  = static_cast<int>(((generation >> 2u) & 0x3u) * 2u) - 2; // -2,0,2,4
    return {bx + sx, by, bz + sz};
}

auto getRuntimeId(std::string_view blockName, uint fallback) -> uint {
    auto block = Block::tryGetFromRegistry(blockName);
    if (!block) {
        return fallback;
    }
    return block->computeRawSerializationIdHashForNetwork();
}

auto getChestRuntimeId() -> uint {
    static uint runtimeId = getRuntimeId("minecraft:chest", 0);
    return runtimeId;
}

auto getAirRuntimeId() -> uint {
    static uint runtimeId = getRuntimeId("minecraft:air", 0);
    return runtimeId;
}

void sendBlockUpdate(Player& player, BlockPos const& pos, uint runtimeId) {
    BinaryStream stream;
    stream.writeVarInt(pos.x, nullptr, nullptr);
    stream.writeUnsignedVarInt(static_cast<uint>(pos.y), nullptr, nullptr);
    stream.writeVarInt(pos.z, nullptr, nullptr);
    stream.writeUnsignedVarInt(runtimeId, nullptr, nullptr);
    stream.writeUnsignedVarInt(0, nullptr, nullptr); // layer
    stream.writeUnsignedVarInt(0, nullptr, nullptr); // flags
    NetworkPacket<MinecraftPacketIds::UpdateBlock> packet(stream.mBuffer);
    player.sendNetworkPacket(packet);
    logger.debug(
        "ChestUI: UpdateBlock -> {} pos=({}, {}, {}) runtimeId={}",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        runtimeId
    );
}

void sendChestBlockActor(Player& player, BlockPos const& pos, std::string const& title) {
    CompoundTag tag;
    tag["Findable"]   = static_cast<char>(0);
    tag["id"]         = "Chest";
    tag["x"]          = pos.x;
    tag["y"]          = pos.y;
    tag["z"]          = pos.z;
    tag["CustomName"] = title;
    tag["isMovable"]  = static_cast<char>(1);
    tag["Items"]      = ListTag{};

    BlockActorDataPacket packet(BlockActorDataPacketPayload(pos, std::move(tag)));
    player.sendNetworkPacket(packet);
    logger.debug(
        "ChestUI: BlockActorData -> {} pos=({}, {}, {}) title={}",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        title
    );
}

void sendContainerOpen(Player& player, ContainerID containerId, BlockPos const& pos) {
    BinaryStream stream;
    stream.writeByte(static_cast<uchar>(static_cast<schar>(containerId)), nullptr, nullptr); // container id
    stream.writeByte(0, nullptr, nullptr);                                                   // container type (chest)
    stream.writeVarInt(pos.x, nullptr, nullptr);
    stream.writeUnsignedVarInt(static_cast<uint>(pos.y), nullptr, nullptr);
    stream.writeVarInt(pos.z, nullptr, nullptr);
    stream.writeVarInt64(-1, nullptr, nullptr); // runtime entity id
    NetworkPacket<MinecraftPacketIds::ContainerOpen> packet(stream.mBuffer);
    player.sendNetworkPacket(packet);
    logger.debug(
        "ChestUI: ContainerOpen -> {} containerId={} pos=({}, {}, {})",
        player.getRealName(),
        static_cast<int>(containerId),
        pos.x,
        pos.y,
        pos.z
    );
}

void sendContainerClose(Player& player, ContainerID containerId, bool serverInitiated) {
    ContainerClosePacket packet;
    packet.mContainerId          = containerId;
    packet.mContainerType        = SharedTypes::Legacy::ContainerType::Container;
    packet.mServerInitiatedClose = serverInitiated;
    player.sendNetworkPacket(packet);
    logger.debug(
        "ChestUI: ContainerClose -> {} containerId={} serverInitiated={}",
        player.getRealName(),
        static_cast<int>(containerId),
        serverInitiated
    );
}

auto normalizeItems(std::vector<ItemStack> const& source) -> std::vector<ItemStack> {
    std::vector<ItemStack> out(kChestSlotCount, ItemStack::EMPTY_ITEM());
    auto const             count = std::min(source.size(), out.size());
    for (size_t i = 0; i < count; ++i) {
        out[i] = source[i];
    }
    return out;
}

void sendContainerSlots(Player& player, ContainerID containerId, std::vector<ItemStack> const& items) {
    FullContainerName fullName;
    fullName.mName      = ContainerEnumName::DynamicContainer;
    fullName.mDynamicId = static_cast<uint>(static_cast<uchar>(containerId));

    InventoryContentPacket contentPacket(containerId, items, fullName, ItemStack::EMPTY_ITEM());
    player.sendNetworkPacket(contentPacket);

    size_t sent = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].isNull()) {
            continue;
        }
        InventorySlotPacket packet(containerId, static_cast<uint>(i), items[i]);
        player.sendNetworkPacket(packet);
        ++sent;
    }
    logger.debug(
        "ChestUI: InventorySlot* -> {} containerId={} totalSlots={} sentNonNull={}",
        player.getRealName(),
        static_cast<int>(containerId),
        items.size(),
        sent
    );
}

struct ActionCaptureContext {
    bool                       active{false};
    std::string                playerKey;
    ItemStackRequestActionType actionType{ItemStackRequestActionType::Take};
    std::optional<uint8_t>     slot;
};

thread_local ActionCaptureContext gActionCapture;

auto resolveSlotFromAction(
    ItemStackRequestAction const& requestAction,
    Session const&                session
) -> std::optional<uint8_t>;

auto isSessionContainerSlot(Session const& session, FullContainerName const& fullName, uchar slot) -> bool {
    if (slot >= session.items.size()) {
        return false;
    }

    auto const name = static_cast<ContainerEnumName>(fullName.mName);
    if (name == ContainerEnumName::DynamicContainer) {
        std::optional<uint> dynamicId = fullName.mDynamicId;
        return !dynamicId.has_value() || dynamicId.value() == session.dynamicContainerId;
    }

    return name == ContainerEnumName::LevelEntityContainer;
}

auto getSessionSnapshot(Player const& player) -> std::optional<Session> {
    std::scoped_lock lock(gSessionMutex);
    auto             it = gSessions.find(getPlayerKey(player));
    if (it == gSessions.end()) {
        return std::nullopt;
    }
    return it->second;
}

void dispatchClickBySlot(Player& player, uint8_t slot, ItemStackRequestActionType actionType, std::string_view source) {
    auto const    playerKey = getPlayerKey(player);
    ClickCallback clickCallback;
    bool          shouldClose = false;
    ItemStack     item        = ItemStack::EMPTY_ITEM();

    {
        std::scoped_lock lock(gSessionMutex);
        auto             it = gSessions.find(playerKey);
        if (it == gSessions.end() || slot >= it->second.items.size()) {
            return;
        }
        clickCallback = it->second.onClick;
        shouldClose   = it->second.closeOnClick;
        item          = it->second.items[slot];
    }

    logger.debug(
        "ChestUI: click resolved player={} slot={} actionType={} source={}",
        player.getRealName(),
        static_cast<int>(slot),
        static_cast<int>(actionType),
        source
    );

    if (clickCallback) {
        clickCallback(player, ClickContext{.slot = slot, .actionType = actionType, .item = std::move(item)});
    }

    if (shouldClose) {
        close(player);
    }
}

void captureSlotFromResponse(ItemStackRequestActionHandler& handler, ItemStackRequestHandlerSlotInfo const& slotInfo) {
    if (!gActionCapture.active || gActionCapture.slot.has_value()) {
        return;
    }

    auto&      player    = handler.mPlayer;
    auto const playerKey = getPlayerKey(player);
    if (playerKey != gActionCapture.playerKey) {
        return;
    }

    std::scoped_lock lock(gSessionMutex);
    auto             it = gSessions.find(playerKey);
    if (it == gSessions.end()) {
        return;
    }

    auto const&         session         = it->second;
    auto const&         fullNameStorage = slotInfo.mOpenContainerNetId;
    auto const&         fullName        = fullNameStorage.get();
    auto const isSession = isSessionContainerSlot(session, fullName, static_cast<uchar>(slotInfo.mSlot));

    if (isSession) {
        gActionCapture.slot = static_cast<uint8_t>(slotInfo.mSlot);
    }
}

auto captureSlotFromValidatedRequest(
    ItemStackRequestActionHandler&    handler,
    ItemStackRequestSlotInfo const&   requestSlotInfo,
    ItemStackRequestHandlerSlotInfo const& resolvedSlotInfo,
    bool                              isItemRequired,
    bool                              isHintSlot
) -> void {
    (void)isItemRequired;
    (void)isHintSlot;

    if (!gActionCapture.active || gActionCapture.slot.has_value()) {
        return;
    }

    auto&      player    = handler.mPlayer;
    auto const playerKey = getPlayerKey(player);
    if (playerKey != gActionCapture.playerKey) {
        return;
    }

    std::scoped_lock lock(gSessionMutex);
    auto             it = gSessions.find(playerKey);
    if (it == gSessions.end()) {
        return;
    }

    auto const&         session         = it->second;
    auto const&         reqFullName     = requestSlotInfo.mFullContainerName;
    auto const&         fullNameStorage = resolvedSlotInfo.mOpenContainerNetId;
    auto const&         fullName        = fullNameStorage.get();
    auto const          reqIsSession    = isSessionContainerSlot(session, reqFullName, requestSlotInfo.mSlot);
    auto const          isSession       = isSessionContainerSlot(session, fullName, static_cast<uchar>(resolvedSlotInfo.mSlot));

    if (isSession) {
        gActionCapture.slot = static_cast<uint8_t>(resolvedSlotInfo.mSlot);
    } else if (reqIsSession && requestSlotInfo.mSlot < session.items.size()) {
        gActionCapture.slot = requestSlotInfo.mSlot;
    }
}

auto resolveSlotFromAction(
    ItemStackRequestAction const& requestAction,
    Session const&                session
) -> std::optional<uint8_t> {
    auto const actionType = static_cast<ItemStackRequestActionType>(requestAction.mActionType);
    if (actionType != ItemStackRequestActionType::Take && actionType != ItemStackRequestActionType::Place
        && actionType != ItemStackRequestActionType::Swap && actionType != ItemStackRequestActionType::Drop
        && actionType != ItemStackRequestActionType::Destroy && actionType != ItemStackRequestActionType::Consume) {
        return std::nullopt;
    }

    auto const*         transfer = static_cast<ItemStackRequestActionTransferBase const*>(&requestAction);
    auto const&         src      = *transfer->mSrc;
    auto const&         dst      = *transfer->mDst;
    auto const srcIsSession = isSessionContainerSlot(session, src.mFullContainerName, src.mSlot);
    auto const dstIsSession = isSessionContainerSlot(session, dst.mFullContainerName, dst.mSlot);

    if (srcIsSession) {
        return static_cast<uint8_t>(src.mSlot);
    }
    if (dstIsSession) {
        return static_cast<uint8_t>(dst.mSlot);
    }
    return std::nullopt;
}

auto takeSession(Player const& player) -> std::optional<Session> {
    auto const       key = getPlayerKey(player);
    std::scoped_lock lock(gSessionMutex);
    auto             it = gSessions.find(key);
    if (it == gSessions.end()) {
        return std::nullopt;
    }
    Session session = std::move(it->second);
    gSessions.erase(it);
    return session;
}

void closeByClientPacket(Player& player, Session session) {
    sendBlockUpdate(player, session.fakePos, session.originalRuntimeId);
    if (session.onClose) {
        session.onClose(player);
    }
}

void cleanupForReopen(Player& player) {
    auto sessionOpt = takeSession(player);
    if (!sessionOpt.has_value()) {
        return;
    }
    auto session = std::move(sessionOpt.value());
    sendBlockUpdate(player, session.fakePos, session.originalRuntimeId);
    if (session.onClose) {
        session.onClose(player);
    }
}

void runAfterTicks(int ticks, std::function<void()> task) {
    if (ticks <= 0) {
        ll::thread::ServerThreadExecutor::getDefault().execute(std::move(task));
        return;
    }
    ll::thread::ServerThreadExecutor::getDefault().executeAfter(std::move(task), std::chrono::milliseconds(ticks * 50));
}

} // namespace

bool open(Player& player, OpenRequest request) {
    cleanupForReopen(player);

    Session session;
    session.generation         = ++gSessionGeneration;
    int resolvedContainerId    = request.containerId;
    session.containerId        = static_cast<ContainerID>(resolvedContainerId);
    session.dynamicContainerId = static_cast<uint>(static_cast<uchar>(session.containerId));
    session.openedAt           = std::chrono::steady_clock::now();
    session.fakePos            = makeFakePos(player, session.generation);
    session.originalRuntimeId =
        player.getDimensionBlockSource().getBlock(session.fakePos).computeRawSerializationIdHashForNetwork();
    session.items        = normalizeItems(request.items);
    session.title        = request.title.empty() ? std::string("Chest") : request.title;
    session.onClick      = std::move(request.onClick);
    session.onClose      = std::move(request.onClose);
    session.closeOnClick = request.closeOnClick;

    {
        std::scoped_lock lock(gSessionMutex);
        gSessions[getPlayerKey(player)] = session;
    }

    logger.debug(
        "ChestUI: open {} containerId={} fakePos=({}, {}, {}) itemCount={} closeOnClick={}",
        player.getRealName(),
        static_cast<int>(session.containerId),
        session.fakePos.x,
        session.fakePos.y,
        session.fakePos.z,
        session.items.size(),
        session.closeOnClick
    );

    sendBlockUpdate(player, session.fakePos, getChestRuntimeId());
    sendChestBlockActor(player, session.fakePos, session.title);

    auto const openContainerId = session.containerId;
    auto const openGeneration  = session.generation;
    auto const openPos         = session.fakePos;
    auto const openItems       = session.items;
    auto const playerKey       = getPlayerKey(player);
    auto const playerUuid      = player.getUuid().asString();
    auto const playerXuid      = player.getXuid();

    runAfterTicks(4, [playerKey, playerUuid, playerXuid, openContainerId, openGeneration, openPos, openItems]() {
        auto* player = findPlayerByIdentity(playerUuid, playerXuid);
        if (!player) {
            return;
        }

        {
            std::scoped_lock lock(gSessionMutex);
            auto             it = gSessions.find(playerKey);
            if (it == gSessions.end() || it->second.containerId != openContainerId
                || it->second.generation != openGeneration) {
                return;
            }
        }

        sendContainerOpen(*player, openContainerId, openPos);
        sendContainerSlots(*player, openContainerId, openItems);
    });

    return true;
}

bool update(Player& player, UpdateRequest request) {
    std::string            nextTitle;
    std::vector<ItemStack> nextItems;
    ContainerID            containerId{ContainerID::None};
    BlockPos               fakePos{};

    {
        std::scoped_lock lock(gSessionMutex);
        auto             it = gSessions.find(getPlayerKey(player));
        if (it == gSessions.end()) {
            return false;
        }

        if (request.title.has_value()) {
            it->second.title = std::move(request.title.value());
        }
        it->second.items = normalizeItems(request.items);

        nextTitle   = it->second.title;
        nextItems   = it->second.items;
        containerId = it->second.containerId;
        fakePos     = it->second.fakePos;
    }

    sendChestBlockActor(player, fakePos, nextTitle);
    sendContainerSlots(player, containerId, nextItems);
    return true;
}

bool close(Player& player) {
    auto sessionOpt = takeSession(player);
    if (!sessionOpt.has_value()) {
        return false;
    }
    auto session = std::move(sessionOpt.value());

    sendContainerClose(player, session.containerId, true);
    sendBlockUpdate(player, session.fakePos, session.originalRuntimeId);

    if (session.onClose) {
        session.onClose(player);
    }
    return true;
}

bool isOpen(Player const& player) {
    std::scoped_lock lock(gSessionMutex);
    return gSessions.contains(getPlayerKey(player));
}

void closeAll() {
    std::vector<std::pair<std::string, Session>> sessions;
    {
        std::scoped_lock lock(gSessionMutex);
        sessions.reserve(gSessions.size());
        for (auto& [playerKey, session] : gSessions) {
            sessions.emplace_back(playerKey, std::move(session));
        }
        gSessions.clear();
    }

    auto level = ll::service::getLevel();
    if (!level) {
        return;
    }

    for (auto& [playerKey, session] : sessions) {
        auto uuid = mce::UUID::fromString(playerKey);
        if (uuid == mce::UUID::EMPTY()) {
            continue;
        }
        auto* player = level->getPlayer(uuid);
        if (!player) {
            continue;
        }
        sendContainerClose(*player, session.containerId, true);
        sendBlockUpdate(*player, session.fakePos, session.originalRuntimeId);
        if (session.onClose) {
            session.onClose(*player);
        }
    }
}


LL_AUTO_TYPE_INSTANCE_HOOK(
    ChestUiAddResponseSlotInfoHook,
    ll::memory::HookPriority::Normal,
    ItemStackRequestActionHandler,
    &ItemStackRequestActionHandler::_addResponseSlotInfo,
    void,
    ItemStackRequestHandlerSlotInfo const& slotInfo,
    ItemStack const&                       item
) {
    (void)item;
    captureSlotFromResponse(*this, slotInfo);
    origin(slotInfo, item);
}


LL_AUTO_TYPE_INSTANCE_HOOK(
    ChestUiValidateRequestSlotHook,
    ll::memory::HookPriority::Normal,
    ItemStackRequestActionHandler,
    &ItemStackRequestActionHandler::_validateRequestSlot,
    ItemStackRequestHandlerSlotInfo,
    ItemStackRequestSlotInfo const& requestSlotInfo,
    bool                            isItemRequired,
    bool                            isHintSlot
) {
    auto result = origin(requestSlotInfo, isItemRequired, isHintSlot);
    captureSlotFromValidatedRequest(*this, requestSlotInfo, result, isItemRequired, isHintSlot);
    return result;
}


LL_AUTO_TYPE_INSTANCE_HOOK(
    ChestUiHandleRequestActionHook,
    ll::memory::HookPriority::Normal,
    ItemStackRequestActionHandler,
    &ItemStackRequestActionHandler::handleRequestAction,
    ItemStackNetResult,
    ItemStackRequestAction const& requestAction
) {
    auto&      player    = mPlayer;
    auto const playerKey = getPlayerKey(player);
    bool       tracking  = false;
    std::optional<uint8_t> actionSlot;

    {
        std::scoped_lock lock(gSessionMutex);
        auto             it = gSessions.find(playerKey);
        if (it != gSessions.end()) {
            tracking   = true;
            actionSlot = resolveSlotFromAction(requestAction, it->second);
        }
    }

    if (!tracking) {
        return origin(requestAction);
    }

    auto prevCapture        = gActionCapture;
    gActionCapture.active   = true;
    gActionCapture.playerKey = playerKey;
    gActionCapture.actionType = static_cast<ItemStackRequestActionType>(requestAction.mActionType);
    gActionCapture.slot.reset();

    auto result = origin(requestAction);

    if (gActionCapture.slot.has_value()) {
        dispatchClickBySlot(player, gActionCapture.slot.value(), gActionCapture.actionType, "response");
    } else if (actionSlot.has_value()) {
        logger.debug(
            "ChestUI: request action fallback slot player={} slot={} actionType={}",
            player.getRealName(),
            static_cast<int>(actionSlot.value()),
            static_cast<int>(gActionCapture.actionType)
        );
        dispatchClickBySlot(player, actionSlot.value(), gActionCapture.actionType, "action");
    }

    gActionCapture = std::move(prevCapture);
    return result;
}


LL_AUTO_TYPE_INSTANCE_HOOK(
    ChestUiContainerCloseHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&    source,
    ContainerClosePacket const& packet
) {
    Player*                player = findPlayerByNetId(source);
    std::optional<Session> sessionOpt;
    bool                   matched        = false;
    bool                   retryScheduled = false;
    std::string            retryPlayerKey;
    std::string            retryPlayerUuid;
    std::string            retryPlayerXuid;
    uint64                 retryGeneration = 0;

    if (player) {
        std::scoped_lock lock(gSessionMutex);
        auto             it                = gSessions.find(getPlayerKey(*player));
        auto const       packetContainerId = static_cast<int>(packet.mContainerId);
        auto const       isWildcardClose   = packetContainerId == -1 || packetContainerId == 255;
        auto const       aliveForMs = it != gSessions.end() ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                                            std::chrono::steady_clock::now() - it->second.openedAt
                                                        )
                                                            .count()
                                                            : 0;
        auto const       idMatched  = it != gSessions.end() && it->second.containerId == packet.mContainerId;
        auto const       earlyCloseGuard = aliveForMs < 250;
        if (it != gSessions.end() && !earlyCloseGuard && idMatched) {
            sessionOpt = std::move(it->second);
            gSessions.erase(it);
            matched = true;
        }

        // Spurious wildcard close can arrive shortly after open; retry once instead of dropping session.
        if (it != gSessions.end() && packetContainerId == -1 && !idMatched && aliveForMs < 1500
            && it->second.reopenAttempts < 1) {
            ++it->second.reopenAttempts;
            retryScheduled  = true;
            retryPlayerKey  = getPlayerKey(*player);
            retryPlayerUuid = player->getUuid().asString();
            retryPlayerXuid = player->getXuid();
            retryGeneration = it->second.generation;
        }

        logger.debug(
            "ChestUI: recv close check player={} packetId={} idMatched={} isWildcard={} aliveMs={} earlyGuard={} "
            "matched={} retryScheduled={}",
            player->getRealName(),
            packetContainerId,
            idMatched,
            isWildcardClose,
            aliveForMs,
            earlyCloseGuard,
            matched,
            retryScheduled
        );
    }

    logger.debug(
        "ChestUI: recv ContainerClose from {} containerId={} matchedSession={}",
        player ? player->getRealName() : std::string("<unknown>"),
        static_cast<int>(packet.mContainerId),
        matched
    );

    origin(source, packet);

    if (player && sessionOpt.has_value()) {
        closeByClientPacket(*player, std::move(sessionOpt.value()));
    }

    if (retryScheduled) {
        runAfterTicks(2, [retryPlayerKey, retryPlayerUuid, retryPlayerXuid, retryGeneration]() {
            auto* target = findPlayerByIdentity(retryPlayerUuid, retryPlayerXuid);
            if (!target) {
                return;
            }

            Session snapshot;
            {
                std::scoped_lock lock(gSessionMutex);
                auto             it = gSessions.find(retryPlayerKey);
                if (it == gSessions.end() || it->second.generation != retryGeneration) {
                    return;
                }
                snapshot = it->second;
            }

            sendBlockUpdate(*target, snapshot.fakePos, getChestRuntimeId());
            sendChestBlockActor(*target, snapshot.fakePos, snapshot.title);
            sendContainerOpen(*target, snapshot.containerId, snapshot.fakePos);
            sendContainerSlots(*target, snapshot.containerId, snapshot.items);

            logger.debug(
                "ChestUI: retry open {} containerId={} pos=({}, {}, {}) attempt={}",
                target->getRealName(),
                static_cast<int>(snapshot.containerId),
                snapshot.fakePos.x,
                snapshot.fakePos.y,
                snapshot.fakePos.z,
                snapshot.reopenAttempts
            );
        });
    }
}

} // namespace CT::ChestUI
