#include "prison/game.hpp"

namespace prison {
namespace {
constexpr double timeLimit = 9e15;
constexpr double incomeLimit = 1e300;

bool validAmount(const Json& value, double limit, bool whole = false) {
    if (!value.isNumeric())
        return false;
    const double amount = value.asDouble();
    return std::isfinite(amount) && amount >= 0 && amount <= limit &&
           (!whole || amount == std::floor(amount));
}

bool payoutCurrency(const std::string& wallet) {
    return wallet == "money" || wallet == "tokens" || wallet == "rubies" || wallet == "sapphires" ||
           wallet == "points";
}

bool payoutEnchant(const Game& game, const Json& id) {
    if (!id.isString() || !game.data.enchants["defs"].isMember(id.asString()))
        return false;
    const auto& ids = procEnchantIds();
    return std::find(ids.begin(), ids.end(), id.asString()) != ids.end();
}
} // namespace

void Game::resetRecords() {
    miningIncome_.clear();
    state["records"] = Json(::Json::objectValue);
    state["record_tracking"] = Json(::Json::objectValue);
    state["record_tracking"]["prestige_started_at"] = 0.0;
    state["record_tracking"]["prestige"] = 0.0;
}

void Game::loadRecords(const Json& snapshot) {
    miningIncome_.clear();
    state["records"] = Json(::Json::objectValue);
    state["record_tracking"] = Json(::Json::objectValue);
    if (state["devmode"].asBool())
        return;
    const Json saved = snapshot["records"].isObject() ? snapshot["records"] : Json(::Json::objectValue);
    const Json fastest = saved["fastest_prestige"];
    if (fastest.isObject() && validAmount(fastest["seconds"], timeLimit) &&
        fastest["seconds"].asDouble() > 0 && validAmount(fastest["prestige"], timeLimit, true) &&
        fastest["prestige"].asDouble() >= 1 &&
        fastest["prestige"].asDouble() <= state["total_prestige"].asDouble()) {
        state["records"]["fastest_prestige"]["seconds"] = fastest["seconds"];
        state["records"]["fastest_prestige"]["prestige"] = fastest["prestige"];
    }
    for (const char* wallet : {"money", "tokens", "rubies", "sapphires", "points"}) {
        const Json row = saved["enchant_payouts"].isObject() ? saved["enchant_payouts"][wallet] : Json();
        if (row.isObject() && validAmount(row["amount"], incomeLimit) && row["amount"].asDouble() > 0 &&
            payoutEnchant(*this, row["enchant"])) {
            state["records"]["enchant_payouts"][wallet]["amount"] = row["amount"];
            state["records"]["enchant_payouts"][wallet]["enchant"] = row["enchant"];
        }
    }
    for (const char* key : {"mining_minute_money", "mining_minute_tokens"}) {
        const Json row = saved[key];
        if (row.isObject() && validAmount(row["amount"], incomeLimit) && row["amount"].asDouble() > 0)
            state["records"][key]["amount"] = row["amount"];
    }
    const double now = state["playtime_seconds"].asDouble();
    const Json tracking = snapshot["record_tracking"].isObject() ? snapshot["record_tracking"]
                                                              : Json(::Json::objectValue);
    if (validAmount(tracking["prestige_started_at"], now) &&
        validAmount(tracking["prestige"], timeLimit, true) &&
        tracking["prestige"].asDouble() == state["total_prestige"].asDouble()) {
        state["record_tracking"]["prestige_started_at"] = tracking["prestige_started_at"];
        state["record_tracking"]["prestige"] = tracking["prestige"];
    }
    const Json window = tracking["mining_minute"];
    if (!window.isArray() || window.size() > 60)
        return;
    double previous = -1;
    for (const auto& row : window) {
        if (!row.isObject() || !validAmount(row["second"], std::floor(now), true) ||
            row["second"].asDouble() <= previous || !validAmount(row["money"], incomeLimit) ||
            !validAmount(row["tokens"], incomeLimit)) {
            miningIncome_.clear();
            return;
        }
        previous = row["second"].asDouble();
        if (previous > std::floor(now) - 60)
            miningIncome_.push_back({previous, row["money"].asDouble(), row["tokens"].asDouble()});
    }
}

void Game::saveRecords(Json& snapshot) const {
    auto& window = snapshot["record_tracking"]["mining_minute"];
    window = Json(::Json::arrayValue);
    const double now = std::floor(state["playtime_seconds"].asDouble());
    for (const auto& income : miningIncome_) {
        if (income.second <= now - 60)
            continue;
        Json row;
        row["second"] = income.second;
        row["money"] = income.money;
        row["tokens"] = income.tokens;
        window.append(std::move(row));
    }
}

void Game::completePrestigeRecord() {
    if (state["devmode"].asBool())
        return;
    const double now = state["playtime_seconds"].asDouble();
    const double prestige = state["total_prestige"].asDouble();
    auto& tracking = state["record_tracking"];
    if (tracking.isObject() && validAmount(tracking["prestige_started_at"], now) &&
        validAmount(tracking["prestige"], timeLimit, true) && tracking["prestige"].asDouble() == prestige) {
        const double elapsed = now - tracking["prestige_started_at"].asDouble();
        auto& fastest = state["records"]["fastest_prestige"];
        if (elapsed > 0 && (!fastest.isObject() || elapsed < fastest["seconds"].asDouble())) {
            fastest["seconds"] = elapsed;
            fastest["prestige"] = prestige + 1;
        }
    }
    // old saves begin a complete timed cycle at their next prestige.
    tracking["prestige_started_at"] = now;
    tracking["prestige"] = prestige + 1;
}

void Game::recordMiningIncome(double money, double tokens) {
    if (state["devmode"].asBool())
        return;
    money = std::isfinite(money) ? std::clamp(money, 0.0, incomeLimit) : 0;
    tokens = std::isfinite(tokens) ? std::clamp(tokens, 0.0, incomeLimit) : 0;
    if (money <= 0 && tokens <= 0)
        return;
    // one bucket per playtime second keeps the rolling window bounded.
    const double now = std::floor(state["playtime_seconds"].asDouble());
    while (!miningIncome_.empty() && miningIncome_.front().second <= now - 60)
        miningIncome_.pop_front();
    if (miningIncome_.empty() || miningIncome_.back().second != now)
        miningIncome_.push_back({now, 0, 0});
    auto& latest = miningIncome_.back();
    latest.money = std::min(incomeLimit, latest.money + money);
    latest.tokens = std::min(incomeLimit, latest.tokens + tokens);
    double minuteMoney = 0, minuteTokens = 0;
    for (const auto& income : miningIncome_) {
        minuteMoney = std::min(incomeLimit, minuteMoney + income.money);
        minuteTokens = std::min(incomeLimit, minuteTokens + income.tokens);
    }
    if (minuteMoney > state["records"]["mining_minute_money"].get("amount", 0).asDouble())
        state["records"]["mining_minute_money"]["amount"] = minuteMoney;
    if (minuteTokens > state["records"]["mining_minute_tokens"].get("amount", 0).asDouble())
        state["records"]["mining_minute_tokens"]["amount"] = minuteTokens;
}

void Game::recordEnchantPayout(const std::string& id, const std::string& wallet, double amount) {
    if (state["devmode"].asBool() || !payoutCurrency(wallet) || !payoutEnchant(*this, Json(id)) ||
        !std::isfinite(amount) || amount <= 0)
        return;
    auto& best = state["records"]["enchant_payouts"][wallet];
    amount = std::min(incomeLimit, amount);
    if (amount > best.get("amount", 0).asDouble()) {
        best["amount"] = amount;
        best["enchant"] = id;
    }
}
} // namespace prison
