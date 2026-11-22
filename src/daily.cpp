#include "prison/game.hpp"

namespace prison {
void Game::loadDailyRewards() {
    const Json saved = state["daily_rewards"].isObject() ? state["daily_rewards"] : Json(::Json::objectValue);
    Json daily;
    const auto& day = saved["last_claim_day"];
    daily["last_claim_day"] = day.isInt64() && day.asInt64() >= 0 && day.asInt64() <= 3652059
                                  ? day : Json(-1);
    for (const char* key : {"streak", "best_streak"})
        daily[key] = saved[key].isInt() ? std::clamp(saved[key].asInt(), 0, 1000000000) : 0;
    daily["streak"] = daily["last_claim_day"].asInt64() < 0 ? 0 : std::max(1, daily["streak"].asInt());
    daily["best_streak"] = std::max(daily["streak"].asInt(), daily["best_streak"].asInt());
    state["daily_rewards"] = std::move(daily);
}

Json Game::dailyReward(std::time_t now) const {
    const auto day = static_cast<::Json::Int64>(std::max<std::time_t>(0, now) / 86400);
    const auto& saved = state["daily_rewards"];
    const auto last = saved["last_claim_day"].asInt64();
    int streak = 1;
    if (last >= 0 && day <= last)
        streak = saved["streak"].asInt();
    else if (last >= 0 && day == last + 1)
        streak = std::min(1000000000, saved["streak"].asInt() + 1);

    const auto& config = data.systems["daily_rewards"];
    Json reward = config["milestones"][0];
    for (const auto& milestone : config["milestones"])
        if (state["total_prestige"].asDouble() >= milestone["prestige"].asInt())
            reward = milestone;
    const double rebirthBonus = 1 + std::min(100.0, state["rebirth"].asDouble()) * .5;
    const double streakBonus = 1 + (std::min(streak, 30) - 1) * .1;
    for (const char* wallet : {"money", "tokens"})
        reward[wallet] = std::floor(reward[wallet].asDouble() * rebirthBonus * streakBonus);
    reward["points"] = int(std::floor(reward["points"].asDouble() * rebirthBonus * streakBonus));
    reward["crystal_tier"] = std::min(10, reward["crystal_tier"].asInt() +
                                            int(std::min(2.0, state["rebirth"].asDouble())) +
                                            std::min(2, streak / 7));
    reward["available"] = now >= 0 && day > last;
    reward["day"] = day;
    reward["streak"] = streak;
    reward["rebirth_multiplier"] = rebirthBonus;
    reward["streak_multiplier"] = streakBonus;
    reward["wait_seconds"] = double((std::max(day, last) + 1) * 86400) - double(now);
    return reward;
}

Result Game::claimDailyReward(std::time_t now) {
    const Json reward = dailyReward(now);
    if (!reward["available"].asBool())
        return {false, "Daily reward unavailable. Use /daily to see the next claim time."};
    for (const char* wallet : {"money", "tokens", "points"})
        if (!state[wallet].isNumeric() || !std::isfinite(state[wallet].asDouble()) ||
            state[wallet].asDouble() < 0 ||
            state[wallet].asDouble() > (std::string(wallet) == "points" ? 9e15 : data.balance["currency_cap"].asDouble()))
            return {false, "Invalid currency balance."};

    Json crystal;
    crystal["id"] = "crystal";
    crystal["tier"] = reward["crystal_tier"];
    crystal["count"] = 1;
    crystal["identified"] = false;
    // keep the whole claim available when its crystal cannot fit.
    if (!giveItem(crystal))
        return {false, "Make room for the daily crystal, then claim again."};
    for (const char* wallet : {"money", "tokens", "points"}) {
        const double before = state[wallet].asDouble();
        const double cap = std::string(wallet) == "points" ? 9e15 : data.balance["currency_cap"].asDouble();
        state[wallet] = std::min(cap, before + reward[wallet].asDouble());
        addStatistic(std::string(wallet) + "_earned", state[wallet].asDouble() - before);
    }
    auto& daily = state["daily_rewards"];
    daily["last_claim_day"] = reward["day"];
    daily["streak"] = reward["streak"];
    daily["best_streak"] = std::max(daily["best_streak"].asInt(), reward["streak"].asInt());
    addStatistic("daily_rewards_claimed");
    return {true, "Daily reward claimed · " + reward["streak"].asString() + " day streak.", "daily:claimed"};
}
} // namespace prison
