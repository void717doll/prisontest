#include "prison/game.hpp"

namespace prison {
namespace {
double savedTimer(const Json& row, const char* key, double maximum) {
    if (!row.isObject() || !row[key].isNumeric() || !std::isfinite(row[key].asDouble()))
        return 0;
    return std::clamp(row[key].asDouble(), 0.0, maximum);
}
} // namespace

void Game::loadEndgame() {
    const Json saved = state["endgame"];
    state["endgame"] = Json(::Json::objectValue);
    recentIncome_.clear();
    for (const auto& value : data.systems["endgame"]) {
        const auto id = value.asString();
        const auto& cfg = data.enchants["effects"][id];
        const Json old = saved.isObject() ? saved[id] : Json();
        state["endgame"][id]["remaining"] = savedTimer(old, "remaining", cfg.get("duration", 0).asDouble());
        state["endgame"][id]["cooldown"] = savedTimer(old, "cooldown", cfg.get("cooldown", 0).asDouble());
    }
}

void Game::tickEndgame(double seconds) {
    for (const auto& value : data.systems["endgame"]) {
        auto& row = state["endgame"][value.asString()];
        for (const char* key : {"remaining", "cooldown"})
            if (row.get(key, 0).asDouble() > 0)
                row[key] = std::max(0.0, row[key].asDouble() - seconds);
    }
}

double Game::endgameMultiplier(const std::string& id) const {
    const auto& def = data.enchants["defs"][id];
    if (enchantLevel(id) <= 0 || state["pick_level"].asInt() < def.get("required_pickaxe_level", 1).asInt() ||
        state["endgame"][id].get("remaining", 0).asDouble() <= 0)
        return 1;
    const double cap = id == "hyper_surge" ? 1.5 : id == "rage" ? 3 : 2;
    return std::clamp(data.enchants["effects"][id].get("multiplier", 1).asDouble(), 1.0, cap);
}

bool Game::endgameReady(const std::string& id) const {
    const auto& def = data.enchants["defs"][id];
    if (enchantLevel(id) <= 0 || state["pick_level"].asInt() < def.get("required_pickaxe_level", 1).asInt() ||
        state["endgame"][id].get("cooldown", 0).asDouble() > 0 ||
        state["endgame"][id].get("remaining", 0).asDouble() > 0)
        return false;
    if (id == "adrenaline" && !state["pet_data"]["active_pet"].isObject())
        return false;
    return true;
}

void Game::recordIncome(const MiningReward& reward) {
    if (enchantLevel("rewind") <= 0 || (reward.money <= 0 && reward.tokens <= 0))
        return;
    const double now = std::floor(state["playtime_seconds"].asDouble());
    const double window =
        std::clamp(data.enchants["effects"]["rewind"].get("income_seconds", 8).asDouble(), 1.0, 8.0);
    while (!recentIncome_.empty() && recentIncome_.front().second <= now - window)
        recentIncome_.pop_front();
    if (recentIncome_.empty() || recentIncome_.back().second != now)
        recentIncome_.push_back({now, 0, 0});
    auto& income = recentIncome_.back();
    const double cap = data.balance.get("currency_cap", 9e33).asDouble();
    income.money = std::min(cap, income.money + reward.money);
    income.tokens = std::min(cap, income.tokens + reward.tokens);
}

void Game::addPetExperience(int amount) {
    Json& pet = state["pet_data"]["active_pet"];
    if (!pet.isObject() || amount <= 0)
        return;
    const auto& cfg = data.systems["pets"];
    const int per = std::max(1, cfg.get("exp_per_level", 100).asInt());
    const int maximum = cfg.get("max_level", 75).asInt();
    int level = pet.get("level", 1).asInt();
    int xp = pet.get("exp", 0).asInt() + std::min(amount, per * maximum);
    while (xp >= per && level < maximum) {
        xp -= per;
        ++level;
    }
    pet["level"] = level;
    pet["exp"] = level >= maximum ? std::min(xp, per - 1) : xp;
}

bool Game::activateEndgame(const std::string& id, std::vector<Proc>& procs) {
    if (!endgameReady(id))
        return false;
    const auto& cfg = data.enchants["effects"][id];
    if (id == "energizer") {
        std::vector<std::string> choices;
        for (const auto& entry : cfg["activates"]) {
            const auto effect = entry.asString();
            // energizer cannot recursively activate itself, rewind or a currency finder.
            const bool area =
                effect == "explosive" || effect == "jackhammer" || effect == "veinminer" || effect == "drill";
            const bool timed = effect == "hyper_surge" || effect == "adrenaline" || effect == "rage" ||
                               effect == "overdrive";
            if ((!area && !timed) || !endgameReady(effect))
                continue;
            if ((area || effect == "rage") && inShaft() && shaftCondition().get("procs", true) != true)
                continue;
            if (std::any_of(procs.begin(), procs.end(), [&](const Proc& proc) { return proc.id == effect; }))
                continue;
            choices.push_back(effect);
        }
        if (choices.empty())
            return false;
        const auto effect =
            choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(random)];
        if (state["endgame"].isMember(effect))
            activateEndgame(effect, procs);
        else {
            procs.push_back({effect, enchantLevel(effect)});
            recordEnchantProc(effect);
        }
    } else if (id == "rewind") {
        double money = 0, tokens = 0;
        const double now = state["playtime_seconds"].asDouble(),
                     window = std::clamp(cfg.get("income_seconds", 8).asDouble(), 1.0, 8.0);
        const double cap = data.balance.get("currency_cap", 9e33).asDouble();
        for (const auto& income : recentIncome_)
            if (income.second > now - window) {
                money = std::min(cap, money + income.money);
                tokens = std::min(cap, tokens + income.tokens);
            }
        if (money <= 0 && tokens <= 0)
            return false;
        const double beforeMoney = state["money"].asDouble(), beforeTokens = state["tokens"].asDouble();
        state["money"] = std::min(cap, beforeMoney + money);
        state["tokens"] = std::min(cap, beforeTokens + tokens);
        addStatistic("money_earned", state["money"].asDouble() - beforeMoney);
        addStatistic("tokens_earned", state["tokens"].asDouble() - beforeTokens);
        state["_summary_money"] = std::min(cap, state.get("_summary_money", 0).asDouble() + money);
        state["_summary_tokens"] = std::min(cap, state.get("_summary_tokens", 0).asDouble() + tokens);
        // replayed income never enters the history or advances mining progression.
        recentIncome_.clear();
    } else if (id == "jackpot") {
        awardJackpot();
    } else if (id == "credit_finder") {
        const double beforePoints = state["points"].asDouble();
        state["points"] = std::min(9e15, beforePoints + 1);
        addStatistic("points_earned", state["points"].asDouble() - beforePoints);
        state["_summary_points"] = state.get("_summary_points", 0).asDouble() + 1;
    } else {
        if (id == "rage" && inShaft() && shaftCondition().get("procs", true) != true)
            return false;
        state["endgame"][id]["remaining"] = cfg["duration"];
    }
    state["endgame"][id]["cooldown"] = cfg["cooldown"];
    recordEnchantProc(id);
    return true;
}

void Game::rollEndgame(std::vector<Proc>& procs) {
    if (endgameMultiplier("hyper_surge") > 1 && state["pet_data"]["active_pet"].isObject()) {
        auto& pet = state["pet_data"]["active_pet"];
        const int interval =
            std::max(1, data.enchants["effects"]["hyper_surge"].get("pet_blocks_per_xp", 20).asInt());
        const int progress = pet.get("surge_blocks", 0).asInt() + 1;
        pet["surge_blocks"] = progress % interval;
        addPetExperience(progress / interval);
    }
    for (const auto& entry : data.systems["endgame"]) {
        const auto id = entry.asString();
        if (!endgameReady(id))
            continue;
        const auto& cfg = data.enchants["effects"][id];
        const int level = std::min(enchantLevel(id), data.enchants["defs"][id]["max"].asInt());
        const double chance =
            std::min(cfg["chance_cap"].asDouble(), level * cfg["chance_per_level"].asDouble());
        if (std::generate_canonical<double, 53>(random) < chance)
            activateEndgame(id, procs);
    }
}
} // namespace prison
