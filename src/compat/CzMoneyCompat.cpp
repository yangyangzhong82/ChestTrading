#include "compat/CzMoneyCompat.h"

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "logger.h"

#include <Windows.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string_view>

namespace CT::Compat {

namespace {

using namespace std::chrono_literals;

constexpr char const* kCzMoneyModName = "czmoney";
constexpr char const* kGetPlayerBalanceSymbol =
    "?getPlayerBalance@api@czmoney@@YA?AV?$optional@N@std@@V?$basic_string_view@DU?$char_traits@D@std@@@4@0@Z";
constexpr char const* kAddPlayerBalanceSymbol =
    "?addPlayerBalance@api@czmoney@@YA?AW4MoneyApiResult@12@V?$basic_string_view@DU?$char_traits@D@std@@@std@@0N000@Z";
constexpr char const* kSubtractPlayerBalanceSymbol =
    "?subtractPlayerBalance@api@czmoney@@YA?AW4MoneyApiResult@12@V?$basic_string_view@DU?$char_traits@D@std@@@std@@0N000@Z";

struct SymbolSet {
    using GetPlayerBalanceFn    = std::optional<double> (*)(std::string_view, std::string_view);
    using AddPlayerBalanceFn    = MoneyApiResult (*)(std::string_view, std::string_view, double, std::string_view, std::string_view, std::string_view);
    using SubtractPlayerBalanceFn =
        MoneyApiResult (*)(std::string_view, std::string_view, double, std::string_view, std::string_view, std::string_view);

    GetPlayerBalanceFn      getPlayerBalance      = nullptr;
    AddPlayerBalanceFn      addPlayerBalance      = nullptr;
    SubtractPlayerBalanceFn subtractPlayerBalance = nullptr;

    [[nodiscard]] bool ready() const { return getPlayerBalance && addPlayerBalance && subtractPlayerBalance; }
};

struct ResolverState {
    mutable std::mutex                       mutex;
    SymbolSet                                symbols;
    HMODULE                                  moduleHandle = nullptr;
    bool                                     available    = false;
    bool                                     loggedLoaded = false;
    bool                                     warnedSymbol = false;
    std::chrono::steady_clock::time_point    nextRetry{};
    mutable std::chrono::steady_clock::time_point nextWarnLog{};
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

    auto mod = ll::mod::ModManagerRegistry::getInstance().getMod(kCzMoneyModName);
    if (!mod || !mod->isEnabled()) {
        resetSymbols(s);
        return false;
    }

    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    if (registry.getModType(kCzMoneyModName) != ll::mod::NativeModManagerName) {
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
    symbols.getPlayerBalance      = resolveSymbol<SymbolSet::GetPlayerBalanceFn>(module, kGetPlayerBalanceSymbol);
    symbols.addPlayerBalance      = resolveSymbol<SymbolSet::AddPlayerBalanceFn>(module, kAddPlayerBalanceSymbol);
    symbols.subtractPlayerBalance = resolveSymbol<SymbolSet::SubtractPlayerBalanceFn>(module, kSubtractPlayerBalanceSymbol);

    if (!symbols.ready()) {
        if (!s.warnedSymbol) {
            logger.warn("检测到 czmoney，但缺少必要符号，已禁用 CzMoney 对接。");
            s.warnedSymbol = true;
        }
        resetSymbols(s);
        return false;
    }

    s.symbols      = symbols;
    s.moduleHandle = module;
    s.available    = true;

    if (!s.loggedLoaded) {
        logger.info("已通过运行时符号启用 czmoney 对接。");
        s.loggedLoaded = true;
    }

    return true;
}

void warnUnavailableLocked(ResolverState& s) {
    auto now = std::chrono::steady_clock::now();
    if (now >= s.nextWarnLog) {
        logger.warn("当前 economyType=CzMoney，但 czmoney 未加载或接口不可用，请安装 czmoney 或将 economyType 改为 LLMoney。");
        s.nextWarnLog = now + 30s;
    }
}

} // namespace

CzMoneyCompat& CzMoneyCompat::getInstance() {
    static CzMoneyCompat instance;
    return instance;
}

void CzMoneyCompat::warnUnavailable() const {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    warnUnavailableLocked(s);
}

std::optional<double> CzMoneyCompat::getPlayerBalance(std::string_view uuid, std::string_view currencyType) const {
    SymbolSet symbols;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (!resolveSymbolsLocked(s)) {
            warnUnavailableLocked(s);
            return std::nullopt;
        }
        symbols = s.symbols;
    }
    return symbols.getPlayerBalance(uuid, currencyType);
}

MoneyApiResult CzMoneyCompat::addPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount) const {
    SymbolSet symbols;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (!resolveSymbolsLocked(s)) {
            warnUnavailableLocked(s);
            return MoneyApiResult::MoneyManagerNotAvailable;
        }
        symbols = s.symbols;
    }
    return symbols.addPlayerBalance(uuid, currencyType, amount, "", "", "");
}

MoneyApiResult CzMoneyCompat::subtractPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount) const {
    SymbolSet symbols;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (!resolveSymbolsLocked(s)) {
            warnUnavailableLocked(s);
            return MoneyApiResult::MoneyManagerNotAvailable;
        }
        symbols = s.symbols;
    }
    return symbols.subtractPlayerBalance(uuid, currencyType, amount, "", "", "");
}

} // namespace CT::Compat
