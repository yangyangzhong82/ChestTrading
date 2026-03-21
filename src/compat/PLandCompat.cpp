#include "compat/PLandCompat.h"

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "logger.h"
#include "mc/platform/UUID.h"

#include <Windows.h>

#include <chrono>
#include <memory>
#include <mutex>

namespace CT {

namespace {

using namespace std::chrono_literals;

constexpr char const* kPLandModName = "PLand";

struct PLandOpaque;
struct LandRegistryOpaque;
struct LandOpaque;

struct RoleEntryLayout {
    bool member;
    bool guest;
};

struct EnvironmentPermsLayout {
    bool allowFireSpread;
    bool allowMonsterSpawn;
    bool allowAnimalSpawn;
    bool allowMobGrief;
    bool allowExplode;
    bool allowFarmDecay;
    bool allowPistonPushOnBoundary;
    bool allowRedstoneUpdate;
    bool allowBlockFall;
    bool allowWitherDestroy;
    bool allowMossGrowth;
    bool allowLiquidFlow;
    bool allowDragonEggTeleport;
    bool allowSculkBlockGrowth;
    bool allowSculkSpread;
    bool allowLightningBolt;
    bool allowMinecartHopperPullItems;
};

struct RolePermsLayout {
    RoleEntryLayout allowDestroy;
    RoleEntryLayout allowPlace;
    RoleEntryLayout useBucket;
    RoleEntryLayout useAxe;
    RoleEntryLayout useHoe;
    RoleEntryLayout useShovel;
    RoleEntryLayout placeBoat;
    RoleEntryLayout placeMinecart;
    RoleEntryLayout useButton;
    RoleEntryLayout useDoor;
    RoleEntryLayout useFenceGate;
    RoleEntryLayout allowInteractEntity;
    RoleEntryLayout useTrapdoor;
    RoleEntryLayout editSign;
    RoleEntryLayout useLever;
    RoleEntryLayout useFurnaces;
    RoleEntryLayout allowPlayerPickupItem;
    RoleEntryLayout allowRideTrans;
    RoleEntryLayout allowRideEntity;
    RoleEntryLayout usePressurePlate;
    RoleEntryLayout allowFishingRodAndHook;
    RoleEntryLayout allowUseThrowable;
    RoleEntryLayout useArmorStand;
    RoleEntryLayout allowDropItem;
    RoleEntryLayout useItemFrame;
    RoleEntryLayout useFlintAndSteel;
    RoleEntryLayout useBeacon;
    RoleEntryLayout useBed;
    RoleEntryLayout allowPvP;
    RoleEntryLayout allowHostileDamage;
    RoleEntryLayout allowFriendlyDamage;
    RoleEntryLayout allowSpecialEntityDamage;
    RoleEntryLayout useContainer;
    RoleEntryLayout useWorkstation;
    RoleEntryLayout useBell;
    RoleEntryLayout useCampfire;
    RoleEntryLayout useComposter;
    RoleEntryLayout useDaylightDetector;
    RoleEntryLayout useJukebox;
    RoleEntryLayout useNoteBlock;
    RoleEntryLayout useCake;
    RoleEntryLayout useComparator;
    RoleEntryLayout useRepeater;
    RoleEntryLayout useLectern;
    RoleEntryLayout useCauldron;
    RoleEntryLayout useRespawnAnchor;
    RoleEntryLayout useBoneMeal;
    RoleEntryLayout useBeeNest;
    RoleEntryLayout editFlowerPot;
    RoleEntryLayout allowUseRangedWeapon;
};

struct LandPermTableLayout {
    EnvironmentPermsLayout environment;
    RolePermsLayout        role;
};

struct SymbolSet {
    using GetInstanceFn    = PLandOpaque& (*)();
    using GetRegistryFn    = LandRegistryOpaque& (*)(PLandOpaque const*);
    // MSVC x64 member function with non-trivial return:
    // RCX=this, RDX=hidden return-buffer, R8=arg1, R9=arg2.
    using GetLandAtFn      = void (*)(LandRegistryOpaque const*, std::shared_ptr<LandOpaque>*, BlockPos const&, int);
    using IsOperatorFn     = bool (*)(LandRegistryOpaque const*, mce::UUID const&);
    using GetPermTypeFn    = int (*)(LandOpaque const*, mce::UUID const&);
    using GetPermTableFn   = LandPermTableLayout const& (*)(LandOpaque const*);

    GetInstanceFn  getInstance  = nullptr;
    GetRegistryFn  getRegistry  = nullptr;
    GetLandAtFn    getLandAt    = nullptr;
    IsOperatorFn   isOperator   = nullptr;
    GetPermTypeFn  getPermType  = nullptr;
    GetPermTableFn getPermTable = nullptr;

    [[nodiscard]] bool ready() const {
        return getInstance && getRegistry && getLandAt && isOperator && getPermType && getPermTable;
    }
};

struct ResolverState {
    std::mutex                         mutex;
    SymbolSet                          symbols;
    HMODULE                            moduleHandle = nullptr;
    bool                               available    = false;
    bool                               loggedLoaded = false;
    bool                               warnedSymbol = false;
    std::chrono::steady_clock::time_point nextErrorLog{};
    std::chrono::steady_clock::time_point nextRetry{};
};

ResolverState& state() {
    static ResolverState s;
    return s;
}

template <typename T>
T resolveSymbol(HMODULE module, char const* symbol) {
    return reinterpret_cast<T>(::GetProcAddress(module, symbol));
}

void resetSymbols(ResolverState& s) {
    s.symbols      = {};
    s.moduleHandle = nullptr;
    s.available    = false;
}

bool resolveSymbolsLocked(ResolverState& s) {
    auto now = std::chrono::steady_clock::now();
    if (s.available && now < s.nextRetry) {
        return true;
    }
    if (!s.available && now < s.nextRetry) {
        return false;
    }
    s.nextRetry = now + 3s;

    auto mod = ll::mod::ModManagerRegistry::getInstance().getMod(kPLandModName);
    if (!mod || !mod->isEnabled()) {
        resetSymbols(s);
        return false;
    }

    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    if (registry.getModType(kPLandModName) != ll::mod::NativeModManagerName) {
        resetSymbols(s);
        return false;
    }
    auto nativeMod = std::static_pointer_cast<ll::mod::NativeMod>(mod);

    auto module = reinterpret_cast<HMODULE>(nativeMod->getHandle());
    if (!module) {
        resetSymbols(s);
        return false;
    }

    if (s.available && s.moduleHandle == module) {
        return true;
    }

    SymbolSet symbols;
    symbols.getInstance = resolveSymbol<SymbolSet::GetInstanceFn>(module, "?getInstance@PLand@land@@SAAEAV12@XZ");
    symbols.getRegistry =
        resolveSymbol<SymbolSet::GetRegistryFn>(module, "?getLandRegistry@PLand@land@@QEBAAEAVLandRegistry@2@XZ");
    symbols.getLandAt = resolveSymbol<SymbolSet::GetLandAtFn>(
        module,
        "?getLandAt@LandRegistry@land@@QEBA?AV?$shared_ptr@VLand@land@@@std@@AEBVBlockPos@@H@Z"
    );
    symbols.isOperator =
        resolveSymbol<SymbolSet::IsOperatorFn>(module, "?isOperator@LandRegistry@land@@QEBA_NAEBVUUID@mce@@@Z");
    symbols.getPermType =
        resolveSymbol<SymbolSet::GetPermTypeFn>(module, "?getPermType@Land@land@@QEBA?AW4LandPermType@2@AEBVUUID@mce@@@Z");
    symbols.getPermTable =
        resolveSymbol<SymbolSet::GetPermTableFn>(module, "?getPermTable@Land@land@@QEBAAEBULandPermTable@2@XZ");

    if (!symbols.ready()) {
        if (!s.warnedSymbol) {
            logger.warn("检测到 PLand，但缺少必要符号，已禁用对接功能。");
            s.warnedSymbol = true;
        }
        resetSymbols(s);
        return false;
    }

    s.symbols      = symbols;
    s.moduleHandle = module;
    s.available    = true;

    if (!s.loggedLoaded) {
        logger.info("已通过运行时符号启用 PLand 对接。");
        s.loggedLoaded = true;
    }

    return true;
}

void reportRuntimeFailureThrottled(char const* message) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    auto now = std::chrono::steady_clock::now();
    if (now >= s.nextErrorLog) {
        logger.warn("{}", message);
        s.nextErrorLog = now + 30s;
    }
    resetSymbols(s);
    s.nextRetry = now + 10s;
}

bool hasRolePermission(
    SymbolSet const&    symbols,
    LandRegistryOpaque& registry,
    LandOpaque const*   land,
    mce::UUID const&    uuid,
    RoleEntryLayout     entry
) {
    if (symbols.isOperator(&registry, uuid)) {
        return true;
    }
    int permType = symbols.getPermType(land, uuid);
    // LandPermType: Operator=0, Owner=1, Member=2, Guest=3.
    if (permType == 0 || permType == 1) {
        return true;
    }
    if (permType == 2) {
        return entry.member;
    }
    if (permType == 3) {
        return entry.guest;
    }
    // Unknown enum value, keep safe fallback to guest policy.
    if (entry.member) {
        return true;
    }
    return entry.guest;
}

enum class LandQueryState {
    Unavailable,
    Available
};

struct LandQueryContext {
    SymbolSet                  symbols;
    LandRegistryOpaque*        registryPtr = nullptr;
    std::shared_ptr<LandOpaque> land;
};

LandQueryState queryLandContext(BlockPos const& pos, int dimId, LandQueryContext& context) {
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (!resolveSymbolsLocked(s)) {
            return LandQueryState::Unavailable;
        }
        context.symbols = s.symbols;
    }

    PLandOpaque* landModPtr = nullptr;
    try {
        landModPtr = std::addressof(context.symbols.getInstance());
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=getInstance，已回退为放行。");
        return LandQueryState::Unavailable;
    }

    try {
        context.registryPtr = std::addressof(context.symbols.getRegistry(landModPtr));
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=getLandRegistry，已回退为放行。");
        return LandQueryState::Unavailable;
    }

    try {
        context.symbols.getLandAt(context.registryPtr, &context.land, pos, dimId);
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=getLandAt，已回退为放行。");
        return LandQueryState::Unavailable;
    }

    return LandQueryState::Available;
}

} // namespace

PLandCompat& PLandCompat::getInstance() {
    static PLandCompat instance;
    return instance;
}

void PLandCompat::probe() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    (void)resolveSymbolsLocked(s);
}

std::optional<bool> PLandCompat::isInLand(Player const& player, BlockPos const& pos) const {
    LandQueryContext context;
    if (queryLandContext(pos, static_cast<int>(player.getDimensionId()), context) == LandQueryState::Unavailable) {
        return std::nullopt;
    }

    return context.land != nullptr;
}

std::optional<bool> PLandCompat::isOwnerLand(std::string const& playerUuid, BlockPos const& pos, int dimId) const {
    if (playerUuid.empty()) {
        return false;
    }

    LandQueryContext context;
    if (queryLandContext(pos, dimId, context) == LandQueryState::Unavailable) {
        return std::nullopt;
    }

    if (!context.land) {
        return false;
    }

    try {
        mce::UUID const uuid = mce::UUID::fromString(playerUuid);
        return context.symbols.getPermType(context.land.get(), uuid) == 1;
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=isOwnerLand，已回退为不匹配。");
        return std::nullopt;
    }
}

bool PLandCompat::canUseContainer(Player const& player, BlockPos const& pos) const {
    return canPlayerDo(player, pos, Action::UseContainer);
}

bool PLandCompat::canPlace(Player const& player, BlockPos const& pos) const {
    return canPlayerDo(player, pos, Action::Place);
}

bool PLandCompat::canDestroy(Player const& player, BlockPos const& pos) const {
    return canPlayerDo(player, pos, Action::Destroy);
}

bool PLandCompat::canPlayerDo(Player const& player, BlockPos const& pos, Action action) const {
    LandQueryContext      context;
    mce::UUID const*      uuidPtr  = nullptr;
    LandPermTableLayout const* tablePtr = nullptr;

    if (queryLandContext(pos, static_cast<int>(player.getDimensionId()), context) == LandQueryState::Unavailable) {
        return true; // PLand not loaded or unavailable -> ignore integration.
    }

    if (!context.land) {
        return true;
    }

    auto const* landPtr = context.land.get();
    try {
        uuidPtr = std::addressof(player.getUuid());
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=getPlayerUuid，已回退为放行。");
        return true;
    }

    try {
        tablePtr = std::addressof(context.symbols.getPermTable(landPtr));
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=getPermTable，已回退为放行。");
        return true;
    }

    try {
        switch (action) {
        case Action::UseContainer:
            return hasRolePermission(context.symbols, *context.registryPtr, landPtr, *uuidPtr, tablePtr->role.useContainer);
        case Action::Place:
            return hasRolePermission(context.symbols, *context.registryPtr, landPtr, *uuidPtr, tablePtr->role.allowPlace);
        case Action::Destroy:
            return hasRolePermission(context.symbols, *context.registryPtr, landPtr, *uuidPtr, tablePtr->role.allowDestroy);
        default:
            return true;
        }
    } catch (...) {
        reportRuntimeFailureThrottled("PLand 对接失败，步骤=evaluatePermission，已回退为放行。");
        return true;
    }
}

} // namespace CT
