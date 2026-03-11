#include "compat/PermissionCompat.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "logger.h"

#include <Windows.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace CT::PermissionCompat {

namespace {

using namespace std::chrono_literals;

constexpr char const* kPermissionModName = "Bedrock-Authority";
constexpr char const* kGetInstanceSymbol = "?getInstance@PermissionManager@permission@BA@@SAAEAV123@XZ";
constexpr char const* kHasPermissionSymbol =
    "?hasPermission@PermissionManager@permission@BA@@QEAA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z";

struct PermissionManagerOpaque;

struct SymbolSet {
    using GetInstanceFn    = PermissionManagerOpaque& (*)();
    using HasPermissionFn  = bool (*)(PermissionManagerOpaque*, std::string const&, std::string const&);

    GetInstanceFn   getInstance   = nullptr;
    HasPermissionFn hasPermission = nullptr;

    [[nodiscard]] bool ready() const { return getInstance && hasPermission; }
};

struct ResolverState {
    std::mutex                            mutex;
    SymbolSet                             symbols;
    HMODULE                               moduleHandle = nullptr;
    bool                                  available    = false;
    bool                                  loggedLoaded = false;
    bool                                  warnedSymbol = false;
    std::chrono::steady_clock::time_point nextRetry{};
    std::chrono::steady_clock::time_point nextWarnLog{};
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

    auto mod = ll::mod::ModManagerRegistry::getInstance().getMod(kPermissionModName);
    if (!mod || !mod->isEnabled()) {
        resetSymbols(s);
        return false;
    }

    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    if (registry.getModType(kPermissionModName) != ll::mod::NativeModManagerName) {
        resetSymbols(s);
        return false;
    }

    auto nativeMod = std::static_pointer_cast<ll::mod::NativeMod>(mod);
    auto module    = reinterpret_cast<HMODULE>(nativeMod->getHandle());
    if (!module) {
        resetSymbols(s);
        return false;
    }

    if (s.available && s.moduleHandle == module) {
        return true;
    }

    SymbolSet symbols;
    symbols.getInstance   = resolveSymbol<SymbolSet::GetInstanceFn>(module, kGetInstanceSymbol);
    symbols.hasPermission = resolveSymbol<SymbolSet::HasPermissionFn>(module, kHasPermissionSymbol);

    if (!symbols.ready()) {
        if (!s.warnedSymbol) {
            logger.warn("检测到 Bedrock-Authority，但缺少必要符号，已回退为放行模式。");
            s.warnedSymbol = true;
        }
        resetSymbols(s);
        return false;
    }

    s.symbols      = symbols;
    s.moduleHandle = module;
    s.available    = true;

    if (!s.loggedLoaded) {
        logger.info("已通过运行时符号启用 Bedrock-Authority 权限组对接。");
        s.loggedLoaded = true;
    }

    return true;
}

void reportRuntimeFailureThrottled(char const* message) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    auto now = std::chrono::steady_clock::now();
    if (now >= s.nextWarnLog) {
        logger.warn("{}", message);
        s.nextWarnLog = now + 30s;
    }
    resetSymbols(s);
    s.nextRetry = now + 10s;
}

} // namespace

bool hasPermission(const std::string& playerUuid, const std::string& permissionNode) {
    SymbolSet symbols;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (!resolveSymbolsLocked(s)) {
            return true;
        }
        symbols = s.symbols;
    }

    PermissionManagerOpaque* manager = nullptr;
    try {
        manager = std::addressof(symbols.getInstance());
    } catch (...) {
        reportRuntimeFailureThrottled("Bedrock-Authority 对接失败，步骤=getInstance，已回退为放行模式。");
        return true;
    }

    try {
        return symbols.hasPermission(manager, playerUuid, permissionNode);
    } catch (...) {
        reportRuntimeFailureThrottled("Bedrock-Authority 对接失败，步骤=hasPermission，已回退为放行模式。");
        return true;
    }
}

} // namespace CT::PermissionCompat
