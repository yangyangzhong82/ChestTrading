#pragma once

#include <optional>
#include <string_view>

namespace CT::Compat {

enum class MoneyApiResult {
    Success = 0,
    AccountNotFound,
    InvalidAmount,
    InsufficientBalance,
    DatabaseError,
    MoneyManagerNotAvailable,
    UnknownError
};

class CzMoneyCompat {
public:
    static CzMoneyCompat& getInstance();

    void warnUnavailable() const;

    std::optional<double> getPlayerBalance(std::string_view uuid, std::string_view currencyType) const;
    MoneyApiResult        addPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount) const;
    MoneyApiResult        subtractPlayerBalance(std::string_view uuid, std::string_view currencyType, double amount) const;

private:
    CzMoneyCompat() = default;
};

} // namespace CT::Compat
