#include "prison/game.hpp"
#include "prison/world.hpp"
#include <cctype>
#include <cstdlib>
#include <limits>

namespace prison {
namespace {
constexpr double progressLimit = 9000000000000000.0;
constexpr double defaultCurrencyCap = 9e33;

double number(const Json& value, double fallback = 0.0, double limit = defaultCurrencyCap) {
    if (!value.isNumeric())
        return fallback;
    const double n = value.asDouble();
    return std::isfinite(n) ? std::clamp(n, 0.0, limit) : fallback;
}
double field(const Json& value, const char* key, double fallback = 0.0, double limit = defaultCurrencyCap) {
    return value.isObject() ? number(value[key], fallback, limit) : fallback;
}
int integer(const Json& value, const char* key, int fallback = 0,
            int limit = std::numeric_limits<int>::max()) {
    return static_cast<int>(field(value, key, fallback, limit));
}
std::string string(const Json& value, const char* key, const std::string& fallback = "") {
    return value.isObject() && value[key].isString() ? value[key].asString() : fallback;
}
bool boolean(const Json& value, const char* key, bool fallback = false) {
    return value.isObject() && value[key].isBool() ? value[key].asBool() : fallback;
}
double cap(const Game& game) {
    return std::max(1.0, field(game.data.balance, "currency_cap", defaultCurrencyCap));
}
double currency(const Game& game, double value) {
    if (std::isnan(value) || value <= 0.0)
        return 0.0;
    return std::min(value, cap(game));
}
bool validNumber(const Json& value, double maximum, bool whole = false) {
    if (!value.isNumeric())
        return false;
    const double number = value.asDouble();
    return std::isfinite(number) && number >= 0 && number <= maximum &&
           (!whole || std::floor(number) == number);
}
bool validProgression(const Game& game) {
    return !game.data.ranks.empty() && validNumber(game.state["money"], cap(game)) &&
           validNumber(game.state["rank_index"], game.data.ranks.size() - 1, true) &&
           validNumber(game.state["total_prestige"], progressLimit, true);
}
double unit(Game& game) {
    return std::generate_canonical<double, 53>(game.random);
}
int roll(Game& game, int low, int high) {
    return std::uniform_int_distribution<int>(low, std::max(low, high))(game.random);
}
std::string rewardKind(std::string kind) {
    kind.erase(std::remove_if(kind.begin(), kind.end(), [](unsigned char c) { return std::isspace(c); }),
               kind.end());
    std::transform(kind.begin(), kind.end(), kind.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (kind == "sell" || kind == "money" || kind == "cash" || kind == "sellboost" || kind == "moneyboost" ||
        kind == "cashboost")
        return "money";
    if (kind == "exp" || kind == "xp" || kind == "expboost" || kind == "xpboost")
        return "xp";
    if (kind == "token" || kind == "tokenboost")
        return "token";
    return "";
}
bool matches(const Json& item, const std::string& kind) {
    return item.isObject() && rewardKind(string(item, "boost_type")) == kind;
}
bool validColor(const std::string& text) {
    return text.size() == 7 && text[0] == '#' &&
           std::all_of(text.begin() + 1, text.end(), [](unsigned char c) { return std::isxdigit(c); });
}

Json cleanItem(const Json& raw, int depth = 0) {
    if (!raw.isObject() || string(raw, "id").empty())
        return Json();
    Json item = raw;
    if (item["id"] == "global_booster")
        item["id"] = "multiplier";
    for (const char* key : {"count", "tier", "amount", "xp", "duration", "remaining", "boost",
                            "base_boost_pct", "level", "exp", "food_value", "last_fed_ts", "raw_progress"}) {
        if (!item.isMember(key) || (std::string(key) == "tier" && item["id"] == "pouch"))
            continue;
        item[key] = number(
            item[key],
            std::string(key) == "count" || std::string(key) == "tier" || std::string(key) == "level" ? 1.0
                                                                                                     : 0.0,
            progressLimit);
    }
    item["count"] = static_cast<::Json::Int64>(field(item, "count", 1.0, 2147483647));
    if (item["count"].asInt64() <= 0)
        return Json();
    // imported metadata must fit the integer fields used by item actions and lore.
    for (const char* key : {"tier", "level", "exp", "food_value"}) {
        if (!item.isMember(key) || (std::string(key) == "tier" && item["id"] == "pouch"))
            continue;
        item[key] = integer(item, key, 1, 2147483646);
    }
    if (item["id"] == "pet") {
        item["level"] = std::clamp(integer(item, "level", 1), 1, 75);
        item["exp"] = std::clamp(integer(item, "exp"), 0, 99);
        if (item.isMember("surge_blocks"))
            item["surge_blocks"] = integer(item, "surge_blocks", 0, 19);
        if (item.isMember("mutation_rolled"))
            item["mutation_rolled"] = boolean(item, "mutation_rolled");
    }
    if (item["id"] == "robot") {
        const std::string kind = rewardKind(string(item, "boost_type"));
        if (kind.empty())
            return Json();
        item["boost_type"] = kind;
        item["count"] = 1;
        item["level"] = std::clamp(integer(item, "level", 1), 1, 4000);
        item["fuel"] = integer(item, "fuel", 0, 1000000000);
        item["fuel_credit"] = integer(item, "fuel_credit", 0, 99);
    }
    if (item["id"] == "enchant_book" && item.isMember("amount"))
        item["amount"] = integer(item, "amount", 1);
    for (const char* key :
         {"tier_id", "boost_type", "slot", "pet_type", "pet_color", "mood", "uid", "instance_id",
          "enchant_key", "custom_name", "description", "specialization", "mutation"}) {
        if (item.isMember(key) && !item[key].isString())
            item.removeMember(key);
    }
    if (item.isMember("pet_color") && !validColor(string(item, "pet_color")))
        item["pet_color"] = "#FFFFFF";
    if (item.isMember("identified"))
        item["identified"] = boolean(item, "identified");
    if (item.isMember("enchanted"))
        item["enchanted"] = boolean(item, "enchanted");
    if (item.isMember("guardian_forged"))
        item["guardian_forged"] = boolean(item, "guardian_forged");
    if (item["id"] == "pouch") {
        if (depth != 0)
            return Json();
        const std::string tier = string(item, "tier", "small");
        item["tier"] = tier == "medium" || tier == "large" ? tier : "small";
        const int slots = std::clamp(integer(item, "slots",
                                             tier == "large"    ? 54
                                             : tier == "medium" ? 27
                                                                : 18),
                                     1, 54);
        item["slots"] = slots;
        const Json contents = item["items"];
        item["items"] = Json(::Json::arrayValue);
        item["items"].resize(slots);
        for (int i = 0; contents.isArray() && i < slots && i < static_cast<int>(contents.size()); ++i)
            item["items"][i] = cleanItem(contents[i], 1);
    }
    return item;
}
Json cleanSlots(const Json& raw, int size) {
    Json slots(::Json::arrayValue);
    slots.resize(size);
    for (int i = 0; raw.isArray() && i < size && i < static_cast<int>(raw.size()); ++i)
        slots[i] = cleanItem(raw[i]);
    return slots;
}
void updateDerived(Game& game) {
    const int rank = integer(game.state, "rank_index");
    game.state["rank_name"] = game.data.ranks.isArray() && rank < static_cast<int>(game.data.ranks.size())
                                  ? string(game.data.ranks[rank], "name", "A")
                                  : "A";
    game.state["pick_xp_needed"] = game.pickXpNeeded(integer(game.state, "pick_level", 1));
    game.state["crystal_slots"] = game.crystalSlots();
}
void addPickXp(Game& game, double amount) {
    double xp = currency(game, field(game.state, "pick_xp") + std::max(0.0, amount));
    int level = std::max(1, integer(game.state, "pick_level", 1));
    // Large developer grants carry remaining XP into future frames instead of
    // making one input event iterate through millions of levels.
    for (int n = 0; n < 32 && level < std::numeric_limits<int>::max() && xp >= game.pickXpNeeded(level);
         ++n) {
        xp -= game.pickXpNeeded(level);
        ++level;
    }
    game.state["pick_xp"] = xp;
    game.state["pick_level"] = level;
    game.state["pick_xp_needed"] = game.pickXpNeeded(level);
}
double pace(const Game& game, const char* kind) {
    const double level = std::max(1, integer(game.state, "pick_level", 1));
    double value = 1.20 - level * .0025;
    if (level > 100)
        value = .95 - (level - 100) * .0005;
    if (level > 1000)
        value = .50 - (level - 1000) * .0000625;
    if (level > 5000)
        value = .25 - (level - 5000) * .000015;
    return std::max(field(game.data.balance, (std::string(kind) + "_pace_floor").c_str(), .18), value);
}
double experienced(int level) {
    if (level <= 0)
        return .68;
    double bonus = level * .028;
    if (level > 100)
        bonus = 2.8 + (level - 100) * .0026;
    if (level > 1000)
        bonus = 5.14 + (level - 1000) * .0008;
    return 1.0 + std::min(7.0, bonus);
}
double fortune(Game& game, int raw, int maximum) {
    const int level = std::clamp(raw, 0, std::max(0, maximum));
    return level > 0 && unit(game) < 1.0 - 2.0 / (level + 2.0) ? roll(game, 2, level + 1) : 1.0;
}
double scaledCost(const Game& game, double base) {
    if (base <= 0)
        return 0;
    const double growth = field(game.data.balance, "prestige_cost_growth_per_level", .1);
    const double power = std::max(1.0, field(game.data.balance, "prestige_cost_growth_power", 7));
    const double total = field(game.state, "total_prestige", progressLimit, progressLimit);
    const double factor = 1.0 + total * growth;
    if (!std::isfinite(factor) || power * std::log(factor) >= std::log(cap(game) / base))
        return cap(game);
    return std::max(base, std::floor(currency(game, base * std::pow(factor, power))));
}
double priceScale(const Game& game, int level) {
    const Json& p = game.data.enchants["progression_scaling"];
    if (level <= 100)
        return field(p, "tier1_base", .45) + level * field(p, "tier1_per_level", .0025);
    if (level <= 1000)
        return field(p, "tier2_base", .70) + (level - 100) * field(p, "tier2_per_level", .0006667);
    if (level <= 5000)
        return field(p, "tier3_base", 1.30) + (level - 1000) * field(p, "tier3_per_level", .00125);
    return field(p, "tier4_base", 6.30) + std::sqrt(level - 5000.0) * field(p, "tier4_sqrt_scale", .10);
}
double singleEnchantCost(const Game& game, const Json& definition, int level) {
    const bool linear = string(definition, "cost_curve") == "linear";
    double price = field(definition, "base_cost", 1) + level * field(definition, "step");
    if (!linear) {
        const Json& scaling = definition.isMember("step_scaling")
                                  ? definition["step_scaling"]
                                  : game.data.enchants["step_scaling"];
        double step = 0;
        for (const auto& threshold : scaling.getMemberNames()) {
            char* end = nullptr;
            const long n = std::strtol(threshold.c_str(), &end, 10);
            if (end == threshold.c_str() || *end || n <= 0)
                continue;
            step += std::floor(static_cast<double>(level) / n) * number(scaling[threshold]);
        }
        price = field(definition, "base_cost", 1) *
                (1.0 + level * field(definition, "step") * (1.0 + step)) * priceScale(game, level);
    }
    const Json& late = definition["late_cost_scaling"];
    const int start = std::max(1, integer(late, "start", 1));
    const double lateScale = late.isObject() && level >= start
                                 ? field(late, "multiplier", 1) *
                                       std::pow(static_cast<double>(level) / start, field(late, "power"))
                                 : 1.0;
    const double cost = currency(game, price * lateScale);
    return linear ? std::ceil(cost) : std::floor(cost);
}
int finderInterval(const Game& game, const char* id, const Json& context = Json()) {
    const Json& cfg = game.data.enchants["effects"][id];
    const int level =
        std::clamp(context.isObject() ? integer(context["enchants"], id) : game.enchantLevel(id), 0,
                   integer(cfg, "max_level", 1000));
    return std::max({1, integer(cfg, "blocks_per_drop_min", 1),
                     integer(cfg, "blocks_per_drop_base", 1) -
                         static_cast<int>(std::floor(level * field(cfg, "blocks_per_level")))});
}
int crystalTier(const Game& game, const Json& context = Json()) {
    const double prestige =
        context.isObject() ? field(context, "prestige") : field(game.state, "total_prestige");
    const int level = context.isObject() ? integer(context["enchants"], "crystalfinder")
                                         : game.enchantLevel("crystalfinder");
    const double tier = 1 + std::floor((prestige + level / 100.0) / 5.0);
    return static_cast<int>(
        std::clamp(tier, 1.0, std::max(1.0, field(game.data.balance, "crystal_drop_tier_cap", 10))));
}
Json item(const char* id) {
    Json result(::Json::objectValue);
    result["id"] = id;
    result["count"] = 1;
    return result;
}
std::string eggTier(const Json& tiers, double chance) {
    double weight = 0;
    for (const auto& tier : tiers)
        if (!boolean(tier, "merge_only"))
            weight += field(tier, "weight");
    double pick = chance * weight;
    for (const auto& tier : tiers) {
        if (boolean(tier, "merge_only"))
            continue;
        pick -= field(tier, "weight");
        if (pick < 0)
            return string(tier, "id", "common");
    }
    return "common";
}
void finderDrops(Game& game, bool bulk, int rolls = 1, const Json& context = Json(), bool autominer = false) {
    if (rolls <= 0)
        return;
    for (const char* id : {"crystalfinder", "partfinder"}) {
        if (roll(game, 1, finderInterval(game, id, context)) > rolls)
            continue;
        const bool part = std::string(id) == "partfinder";
        Json drop = item(part ? "pick_part" : "crystal");
        const int partsPrestige =
            context.isObject() ? integer(context, "parts_prestige") : integer(game.state, "parts_prestige");
        drop["tier"] = part ? std::min(2147483646, partsPrestige) + 1 : crystalTier(game, context);
        drop["identified"] = false;
        game.giveItem(drop, true);
    }
    const Json& books = game.data.systems["enchant_book_whitelist"];
    if (roll(game, 1, bulk ? 24000 : 45000) <= rolls && books.isArray() && !books.empty()) {
        Json drop = item("enchant_book");
        drop["enchant_key"] = books[roll(game, 0, static_cast<int>(books.size()) - 1)];
        drop["amount"] = 1;
        if (game.enchantCurrency(drop["enchant_key"].asString()) == "tokens")
            game.giveItem(drop, true);
    }
    if (bulk || autominer)
        return;
    const double eggs = (context.isObject() ? field(context["specialization"], "eggs", 1)
                                            : game.specializationModifiers()["eggs"].asDouble()) *
                        (1 + game.mutationBonus("eggs"));
    const int eggInterval =
        std::max(1, int(std::ceil(std::max(1, integer(game.data.enchants["effects"]["egg_drop"],
                                                      "blocks_per_drop", 7500)) /
                                  eggs)));
    if (roll(game, 1, eggInterval) == 1) {
        Json drop = item("pet_egg");
        drop["tier_id"] = eggTier(game.data.systems["pets"]["tiers"], unit(game));
        game.giveItem(drop, true);
    }
    if (game.enchantLevel("timefinder") <= 0 || boolean(game.state, "autominer_active"))
        return;
    double progress = field(game.state, "timefinder_progress") + 1.0;
    const int interval = finderInterval(game, "timefinder");
    if (progress >= interval) {
        progress -= interval;
        Json drop = item("automine_time");
        const Json& durations = game.data.enchants["effects"]["timefinder"]["drops_seconds"];
        drop["duration"] = durations.isArray() && !durations.empty()
                               ? durations[roll(game, 0, static_cast<int>(durations.size()) - 1)]
                               : Json(600);
        game.giveItem(drop, true);
    }
    game.state["timefinder_progress"] = progress;
}
void luckyReward(Game& game, MiningReward& reward) {
    const int level = game.enchantLevel("lucky");
    const double now = field(game.state, "playtime_seconds");
    const Json& cfg = game.data.systems["lucky"];
    if (level <= 0 || now < field(game.state, "_lucky_cooldown"))
        return;
    const double chance =
        std::min(field(cfg, "chance_cap", .015),
                 field(cfg, "base_chance", .0004) + level * field(cfg, "per_level_chance", .0001));
    if (unit(game) >= chance)
        return;
    game.state["_lucky_cooldown"] = now + field(cfg, "cooldown_seconds", 8);
    const std::vector<std::string> pool = {"token",      "crystal",  "minebomb", "pet_egg",
                                           "multiplier", "automine", "book"};
    double weight = 0;
    for (const auto& kind : pool)
        weight += field(cfg, (kind + "_weight").c_str());
    double pick = unit(game) * weight;
    std::string chosen = "token";
    for (const auto& kind : pool) {
        pick -= field(cfg, (kind + "_weight").c_str());
        if (pick < 0) {
            chosen = kind;
            break;
        }
    }
    Json drop = item(chosen.c_str());
    if (chosen == "crystal") {
        drop["tier"] = crystalTier(game);
        drop["identified"] = false;
    } else if (chosen == "minebomb")
        drop["tier"] = roll(game, 1, std::min(20, 1 + (level - 1) / 4));
    else if (chosen == "pet_egg") {
        const Json& tiers = game.data.systems["pets"]["tiers"];
        const int count = std::min({5, 1 + (level - 1) / 8, static_cast<int>(tiers.size())});
        if (count > 0)
            drop["tier_id"] = tiers[roll(game, 0, count - 1)]["id"];
        else
            chosen = "token";
    } else if (chosen == "multiplier") {
        drop["amount"] = std::floor(std::min(field(cfg, "multiplier_amount_cap", 4),
                                             field(cfg, "multiplier_amount_base", 1.2) +
                                                 level * field(cfg, "multiplier_amount_per_level", .03)) *
                                        10 +
                                    .5) /
                         10;
        drop["duration"] = std::max(60.0, field(cfg, "multiplier_duration_base", 1200) +
                                              level * field(cfg, "multiplier_duration_per_level", 35));
        drop["boost_type"] = "money";
    } else if (chosen == "automine") {
        drop["id"] = "automine_time";
        drop["duration"] = std::max(60.0, field(cfg, "automine_duration_base", 600) +
                                              level * field(cfg, "automine_duration_per_level", 30));
    } else if (chosen == "book") {
        const Json& books = game.data.systems["enchant_book_whitelist"];
        if (books.isArray() && !books.empty()) {
            drop["id"] = "enchant_book";
            drop["amount"] = 1;
            drop["enchant_key"] = books[roll(game, 0, static_cast<int>(books.size()) - 1)];
        } else
            chosen = "token";
    }
    if (chosen != "token") {
        game.giveItem(drop, true);
        return;
    }
    const double scale = std::clamp(.02 + level * field(cfg, "token_cache_scale_per_level", .011), .02, .38);
    const double tokens =
        std::min(field(cfg, "token_cache_max_tokens", 200000),
                 std::floor(roll(game, integer(game.data.balance, "token_drop_min", 50),
                                 integer(game.data.balance, "token_drop_max", 750)) *
                            scale * std::pow(std::max(.1, game.boost("token")), .4) * pace(game, "token")));
    reward.tokens += tokens;
    reward.money += std::floor(tokens * field(cfg, "token_cache_money_ratio", .6));
}
MiningReward rollMiningReward(Game& game, const std::string& blockId, const std::string& source,
                              const Json& context, bool allowLucky = false) {
    MiningReward reward;
    Json block = game.data.blocks["default"];
    if (game.data.blocks.isObject() && game.data.blocks.isMember(blockId))
        block = game.data.blocks[blockId];
    for (const auto& row : game.data.blocks["blocks"])
        if (string(row, "id") == blockId) {
            block = row;
            break;
        }
    for (const auto& row : game.data.mine["blocks"])
        if (string(row, "id") == blockId) {
            block = row;
            break;
        }
    const bool ore = boolean(block, "token_ore") ||
                     (blockId.size() >= 4 && blockId.compare(blockId.size() - 4, 4, "_ore") == 0);
    const int fortuneLevel = integer(context["enchants"], "fortune", 0, 100000000);
    const double moneyBoost = field(context, "money_mult", 1);
    const double value = field(block, "money", field(block, "sell_value", field(block, "base_money", 1)));
    reward.money = std::floor(currency(
        game, value * field(game.data.balance, "mine_sell_value_multiplier", 8) * moneyBoost *
                  fortune(game, fortuneLevel, integer(game.data.balance, "fortune_effective_cap", 10000)) *
                  field(context, "pace_money", 1)));
    if (ore) {
        const int greed = integer(context["enchants"], "token_greed", 0, 100000000);
        const double amount = roll(game, integer(game.data.balance, "token_drop_min", 50),
                                   integer(game.data.balance, "token_drop_max", 750));
        const double raw = std::floor(
            amount * (1 + greed * .02) *
            fortune(game, greed * 2, integer(game.data.balance, "token_greed_fortune_effective_cap", 250)) *
            2);
        reward.tokens = std::floor(
            currency(game, raw * field(context, "token_mult", 1) * field(context, "pace_token", 1)));
    }
    const int greed = integer(context["enchants"], "greed", 0, 100000000),
              combo = integer(context["enchants"], "combo", 0, 100000000);
    if (greed > 0 &&
        unit(game) < std::min({1.0, field(game.data.balance, "greed_proc_cap", 1),
                               field(game.data.balance, "greed_proc_base", .05) +
                                   greed * field(game.data.balance, "greed_proc_per_level", .008)}))
        reward.money *= 2;
    if (combo > 0 && unit(game) < std::min(.003, combo * .00005)) {
        reward.money *= 2;
        reward.tokens *= 2;
    }
    reward.xp = source == "jackhammer" || source == "veinminer"
                    ? 0
                    : experienced(integer(context["enchants"], "experienced", 0, 100000000)) *
                          field(context, "xp_mult", 1) * field(context, "pace_xp", 1);
    if (allowLucky)
        luckyReward(game, reward);
    if (reward.tokens > 0) {
        const double ratio = std::min(100.0, 3.0 + std::max(0.0, moneyBoost - 1) * 1.75 +
                                                 std::log10(fortuneLevel + 1.0) * 12.5);
        reward.money = std::max(reward.money, std::floor(reward.tokens * ratio));
    }
    if (ore)
        reward.money = 0;
    reward.money =
        currency(game, reward.money * field(context["specialization"], "money", 1) *
                           field(context["robots"], "money", 1, 5) * field(context, "surge_mult", 1, 1.5));
    reward.tokens =
        currency(game, reward.tokens * field(context["specialization"], "token", 1) *
                           field(context["robots"], "token", 1, 5) * field(context, "surge_mult", 1, 1.5));
    reward.xp =
        currency(game, reward.xp * field(context["specialization"], "xp", 1) *
                           field(context["robots"], "xp", 1, 5) * field(context, "overdrive_mult", 1, 2));
    return reward;
}
void applyMiningReward(Game& game, const MiningReward& reward) {
    game.recordIncome(reward);
    game.state["money"] = currency(game, field(game.state, "money") + reward.money);
    game.state["tokens"] = currency(game, field(game.state, "tokens") + reward.tokens);
    game.state["xp"] = currency(game, field(game.state, "xp") + reward.xp);
    game.state["_summary_money"] = currency(game, field(game.state, "_summary_money") + reward.money);
    game.state["_summary_tokens"] = currency(game, field(game.state, "_summary_tokens") + reward.tokens);
    addPickXp(game, reward.xp);
}
void awardBlocks(Game& game, int removed) {
    const double previous = field(game.state, "blocks_mined", 0, progressLimit);
    const double blocks = std::min(progressLimit, previous + removed);
    game.state["blocks_mined"] = blocks;
    const int perPoint = std::max(1, integer(game.data.systems, "points_blocks_per_point", 10000));
    game.state["points"] =
        std::min(progressLimit, field(game.state, "points") + std::floor(blocks / perPoint) -
                                    std::floor(previous / perPoint));
    for (const auto& row : game.data.systems["reward_milestones"]) {
        const double target = field(row, "blocks");
        const std::string key = std::to_string(static_cast<::Json::Int64>(target));
        if (blocks < target || boolean(game.state["reward_claims"], key.c_str()))
            continue;
        game.state["reward_claims"][key] = true;
        for (const char* kind : {"money", "tokens"}) {
            game.state[kind] = currency(game, field(game.state, kind) + field(row, kind));
            const std::string summary = std::string("_summary_") + kind;
            game.state[summary] = currency(game, field(game.state, summary.c_str()) + field(row, kind));
        }
    }
}
double procChance(const Game& game, const std::string& id, int level) {
    if (level <= 0)
        return 0;
    const Json& cfg = game.data.enchants["effects"][id];
    if (id == "explosive") {
        double chance = .004;
        if (level >= 10)
            chance = .008 + (level - 10) * .0006;
        if (level >= 50)
            chance = .032 + (level - 50) * .00055;
        if (level >= 200)
            chance = .115 + (level - 200) * .0002;
        if (level >= 1000)
            chance = .275 + (level - 1000) * .000075;
        if (level >= 5000)
            chance = .575;
        return std::clamp(chance * field(cfg, "explosive_proc_scale", .17), 0.0, 1.0);
    }
    return std::clamp(field(cfg, (id + "_proc_base").c_str()) +
                          level * field(cfg, (id + "_proc_per_level").c_str()),
                      0.0, std::min(1.0, field(cfg, (id + "_proc_cap").c_str())));
}
} // namespace

Game::Game(const Data& source) : data(source) {
    newWorld();
}

void Game::newWorld(bool devmode) {
    autominerElapsed_ = 0;
    state = Json(::Json::objectValue);
    state["devmode"] = devmode;
    state["tutorial"]["status"] = devmode ? "skipped" : "new";
    loadTutorial();
    state["gem_currency_version"] = 1;
    for (const char* key : {"money", "tokens", "rubies", "sapphires", "xp", "pick_xp", "blocks_mined",
                            "timefinder_progress", "rank_index", "prestige_level", "parts_prestige",
                            "rebirth", "total_prestige", "points", "autominer_time", "playtime_seconds"})
        state[key] = 0.0;
    state["pick_level"] = 1;
    state["pick_custom_name"] = "Prison Tool";
    state["enchant_label_color"] = "";
    state["enchant_value_color"] = "";
    state["enchant_colors_enabled"] = true;
    for (const char* key : {"enchants", "reward_claims", "message_settings", "mine_state", "inventory"})
        state[key] = Json(::Json::objectValue);
    state["message_settings"]["visual_enchants"] = false;
    for (const char* kind : {"money", "token", "xp"})
        state["boosts"][kind] = Json(::Json::arrayValue);
    state["inventory_slots"] = cleanSlots(Json(), 36);
    Json pick = item("pickaxe");
    pick["instance_id"] = "pickaxe";
    pick["locked"] = true;
    state["inventory_slots"][0] = pick;
    Json bomb = item("minebomb");
    bomb["tier"] = 1;
    state["inventory_slots"][1] = bomb;
    state["inventory"]["minebomb"] = 1;
    state["selected_hotbar_index"] = 0;
    state["vault_slots"] = cleanSlots(Json(), VaultPageSize);
    state["vault_page_size"] = VaultPageSize;
    state["vault_names"].append("Vault 1");
    state["robot_slots"] = cleanSlots(Json(), 3);
    state["equipped_crystals"] = Json(::Json::arrayValue);
    for (const char* slot : {"head", "binding", "rod"})
        state["equipped_parts"][slot] = Json();
    const int bps = integer(data.systems["autominer"], "default_bps", 3);
    state["autominer_unlocked_bps"] = bps;
    state["autominer_bps"] = bps;
    state["autominer_active"] = false;
    state["pet_data"]["active_pet"] = Json();
    state["pet_data"]["hatchery"] = Json(::Json::arrayValue);
    state["cleaner_settings"] = filterProfile(Json());
    loadProgression();
    loadEquipmentSets();
    loadShaft();
    loadEndgame();
    loadGuardian();
    updateDerived(*this);
}

void Game::loadSnapshot(const Json& snapshot) {
    newWorld();
    if (!snapshot.isObject())
        return;
    for (const auto& key : snapshot.getMemberNames()) {
        if (key.empty() || key[0] != '_')
            state[key] = snapshot[key];
    }
    state["devmode"] = boolean(snapshot, "devmode");
    state["tutorial"] = snapshot["tutorial"];
    loadTutorial();
    if (!state["devmode"].asBool() || !snapshot["dev_mine_prestige"].isInt() ||
        snapshot["dev_mine_prestige"].asInt() < 0 ||
        snapshot["dev_mine_prestige"].asInt() > MineSizePrestigeCap)
        state.removeMember("dev_mine_prestige");
    for (const char* key : {"money", "tokens", "xp", "pick_xp", "autominer_time"})
        state[key] = field(snapshot, key, 0, cap(*this));
    for (const char* wallet : {"rubies", "sapphires"}) {
        double value = field(snapshot, wallet, 0, cap(*this));
        if (integer(snapshot, "gem_currency_version") < 1)
            value *= 1000000;
        state[wallet] = std::floor(currency(*this, value));
    }
    state["gem_currency_version"] = 1;
    for (const char* key : {"blocks_mined", "timefinder_progress", "rank_index", "prestige_level",
                            "parts_prestige", "rebirth", "total_prestige", "points"})
        state[key] = std::floor(field(snapshot, key, 0, progressLimit));
    state["playtime_seconds"] = field(snapshot, "playtime_seconds", 0, progressLimit);
    state["pick_level"] = std::max(1, integer(snapshot, "pick_level", 1));
    state["parts_prestige"] = integer(snapshot, "parts_prestige", 0, 2147483646);
    state["rank_index"] =
        std::clamp(integer(state, "rank_index"), 0, std::max(0, static_cast<int>(data.ranks.size()) - 1));
    const double cycle = std::max(1, integer(data.balance, "prestiges_per_rebirth", 101));
    state["rebirth"] =
        std::min(progressLimit, field(state, "rebirth") + std::floor(field(state, "prestige_level") / cycle));
    state["prestige_level"] = std::fmod(field(state, "prestige_level"), cycle);
    state["total_prestige"] =
        std::min(progressLimit, std::max(field(state, "total_prestige"),
                                         field(state, "rebirth") * cycle + field(state, "prestige_level")));
    state["pick_custom_name"] = string(snapshot, "pick_custom_name", "Prison Tool");
    if (state["pick_custom_name"].asString().empty())
        state["pick_custom_name"] = "Prison Tool";
    for (const char* key : {"enchant_label_color", "enchant_value_color"})
        state[key] = validColor(string(snapshot, key)) ? string(snapshot, key) : "";
    state["enchant_colors_enabled"] = boolean(snapshot, "enchant_colors_enabled", true);
    if (!state["settings"].isObject())
        state["settings"] = Json(::Json::objectValue);
    for (const char* key : {"show_scoreboard", "show_max_labels"})
        state["settings"][key] = boolean(state["settings"], key, true);
    for (const char* key : {"enchants", "reward_claims", "inventory", "message_settings", "mine_state"})
        if (!state[key].isObject())
            state[key] = Json(::Json::objectValue);
    auto& messages = state["message_settings"];
    bool visual = boolean(messages, "visual_enchants");
    for (const char* key : {"visual_explosive", "visual_jackhammer", "visual_veinminer", "visual_drill"}) {
        if (!messages.isMember("visual_enchants"))
            visual = visual || boolean(messages, key);
        messages.removeMember(key);
    }
    messages["visual_enchants"] = visual;
    for (const auto& id : state["enchants"].getMemberNames())
        state["enchants"][id] = integer(state["enchants"], id.c_str(), 0, 100000000);
    for (const auto& id : state["reward_claims"].getMemberNames())
        state["reward_claims"][id] = boolean(state["reward_claims"], id.c_str());
    const Json boosts = snapshot["boosts"];
    state["boosts"] = Json(::Json::objectValue);
    for (const char* kind : {"money", "token", "xp"}) {
        state["boosts"][kind] = Json(::Json::arrayValue);
        if (!boosts.isObject() || !boosts[kind].isArray())
            continue;
        for (::Json::ArrayIndex i = 0; i < boosts[kind].size() && i < 256; ++i) {
            const Json& old = boosts[kind][i];
            if (old.isObject()) {
                Json row;
                row["amount"] = field(old, "amount", 1);
                row["remaining"] = field(old, "remaining");
                state["boosts"][kind].append(row);
            } else if (old.isNumeric())
                state["boosts"][kind].append(number(old, 1));
        }
    }
    Json slots = snapshot["inventory_slots"];
    if (!slots.isArray()) {
        slots = cleanSlots(Json(), 36);
        int at = 1;
        for (const auto& id : state["inventory"].getMemberNames()) {
            if (id == "pickaxe" || at >= 36 || number(state["inventory"][id]) <= 0)
                continue;
            Json old = item(id.c_str());
            old["count"] = state["inventory"][id];
            slots[at++] = old;
        }
    }
    state["inventory_slots"] = cleanSlots(slots, 36);
    Json pick = item("pickaxe");
    pick["instance_id"] = "pickaxe";
    pick["locked"] = true;
    state["inventory_slots"][0] = pick;
    for (int i = 1; i < 36; ++i)
        if (string(state["inventory_slots"][i], "id") == "pickaxe")
            state["inventory_slots"][i] = Json();
    state.removeMember("relics");
    state.removeMember("pick_skin");
    state.removeMember("caves_found");
    const int oldPageSize = integer(snapshot, "vault_page_size", 27) == VaultPageSize ? VaultPageSize : 27;
    const int vaultPages =
        snapshot["vault_slots"].isArray()
            ? std::clamp((int(snapshot["vault_slots"].size()) + oldPageSize - 1) / oldPageSize, 1,
                         MaxVaultPages)
            : 1;
    state["vault_slots"] = cleanSlots(Json(), vaultPages * VaultPageSize);
    // expanding each page separately keeps its items with the saved page name.
    const Json oldVault = cleanSlots(snapshot["vault_slots"], vaultPages * oldPageSize);
    for (int page = 0; page < vaultPages; ++page)
        for (int slot = 0; slot < oldPageSize; ++slot)
            state["vault_slots"][page * VaultPageSize + slot] = oldVault[page * oldPageSize + slot];
    state["vault_page_size"] = VaultPageSize;
    state["robot_slots"] = cleanSlots(snapshot["robot_slots"], 3);
    const char* robotKinds[] = {"money", "xp", "token"};
    for (int i = 0; i < 3; ++i)
        if (string(state["robot_slots"][i], "id") != "robot" ||
            string(state["robot_slots"][i], "boost_type") != robotKinds[i])
            state["robot_slots"][i] = Json();
    state["forge_slots"] = cleanSlots(snapshot["forge_slots"], 4);
    state["vault_names"] = Json(::Json::arrayValue);
    for (int page = 0; page < vaultPages; ++page) {
        std::string name = "Vault " + std::to_string(page + 1);
        const auto& names = snapshot["vault_names"];
        if (names.isArray() && page < int(names.size()) && names[page].isString()) {
            const auto saved = names[page].asString();
            if (!saved.empty() && saved.size() <= 32 &&
                std::none_of(saved.begin(), saved.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                name = saved;
        }
        state["vault_names"].append(name);
    }
    const Json crystals = snapshot["equipped_crystals"];
    state["equipped_crystals"] = cleanSlots(Json(), 14);
    for (int i = 0; crystals.isArray() && i < 14 && i < static_cast<int>(crystals.size()); ++i) {
        Json crystal = crystals[i];
        if (crystal.isObject() && !crystal.isMember("id"))
            crystal["id"] = "crystal";
        state["equipped_crystals"][i] = cleanItem(crystal);
    }
    state["equipped_parts"] = Json(::Json::objectValue);
    for (const char* slot : {"head", "binding", "rod"}) {
        Json part = snapshot["equipped_parts"].isObject() ? snapshot["equipped_parts"][slot] : Json();
        if (part.isObject() && !part.isMember("id"))
            part["id"] = "pick_part";
        state["equipped_parts"][slot] = cleanItem(part);
    }
    const Json pets = snapshot["pet_data"];
    Json active = pets.isObject() ? pets["active_pet"] : Json();
    if (active.isObject() && !active.isMember("id"))
        active["id"] = "pet";
    state["pet_data"] = Json(::Json::objectValue);
    state["pet_data"]["active_pet"] = cleanItem(active);
    state["pet_data"]["hatchery"] = Json(::Json::arrayValue);
    if (pets.isObject() && pets["hatchery"].isArray())
        for (const auto& raw : pets["hatchery"]) {
            if (!raw.isObject() || state["pet_data"]["hatchery"].size() >= 6)
                continue;
            Json egg = raw;
            egg["id"] = "pet";
            egg["count"] = 1;
            egg = cleanItem(egg);
            egg["required"] = std::max(1.0, field(raw, "required", 25000, progressLimit));
            egg["progress"] = field(raw, "progress", 0, egg["required"].asDouble());
            egg["ready"] = egg["progress"].asDouble() >= egg["required"].asDouble();
            state["pet_data"]["hatchery"].append(egg);
        }
    state["cleaner_settings"] = filterProfile(snapshot["cleaner_settings"]);
    state["autominer_unlocked_bps"] = std::clamp(
        integer(snapshot, "autominer_unlocked_bps", std::max(3, integer(snapshot, "autominer_bps", 3))), 3,
        10);
    state["autominer_bps"] =
        std::clamp(integer(snapshot, "autominer_bps", 3), 1, state["autominer_unlocked_bps"].asInt());
    state["autominer_active"] = false;
    state["selected_hotbar_index"] = 0;
    for (const char* key :
         {"_lucky_cooldown", "_summary_elapsed", "_summary_money", "_summary_tokens", "mining_summary"})
        state.removeMember(key);
    if (state["progression"].isObject() && state["progression"]["expeditions"].isArray())
        for (auto& trip : state["progression"]["expeditions"]) {
            if (!trip.isObject())
                continue;
            trip["pet"] = cleanItem(trip["pet"]);
            if (trip["loot"].isArray())
                for (auto& item : trip["loot"])
                    item = cleanItem(item);
        }
    loadProgression();
    loadEquipmentSets();
    loadShaft();
    loadEndgame();
    loadGuardian();
    returnStoredItems();
    updateDerived(*this);
}

Json Game::snapshot() const {
    Json out = state;
    out["autominer_active"] = false;
    for (const auto& key : out.getMemberNames())
        if (!key.empty() && key[0] == '_')
            out.removeMember(key);
    out.removeMember("mining_summary");
    return out;
}

int Game::enchantLevel(const std::string& id) const {
    return integer(state["enchants"], id.c_str(), 0, 100000000);
}
double Game::pickXpNeeded(int level) const {
    const double value = std::max(1, level);
    return std::floor(120.0 + value * 60.0 + value * value * .70);
}
int Game::crystalSlots() const {
    int count = 0;
    for (const auto& required : data.crystals["slot_levels"])
        if (number(required, progressLimit) <= field(state, "pick_level", 1))
            ++count;
    return count;
}
TimedMultiplier Game::activeMultiplier(const std::string& type) const {
    const std::string kind = rewardKind(type);
    TimedMultiplier best;
    if (kind.empty())
        return best;
    for (const auto& row : state["boosts"][kind]) {
        if (!row.isObject())
            continue;
        const double amount = field(row, "amount", 1), remaining = field(row, "remaining");
        if (remaining > 0 && (amount > best.amount || (amount == best.amount && remaining > best.remaining)))
            best = {amount, remaining};
    }
    return best;
}

double Game::boost(const std::string& type) const {
    const std::string kind = rewardKind(type);
    if (kind.empty())
        return 1.0;
    double value =
        1 + field(state, "total_prestige") * field(data.balance, "prestige_money_boost_per_level", .1);
    const double timed = activeMultiplier(kind).amount;
    for (const auto& row : state["boosts"][kind]) {
        if (row.isNumeric())
            value *= number(row, 1);
    }
    double crystals = 0, parts = field(state, "parts_prestige") * .10;
    const Json& equipped = state["equipped_crystals"];
    const int slots = std::min(crystalSlots(), static_cast<int>(equipped.size()));
    for (int i = 0; i < slots; ++i)
        if (matches(equipped[i], kind))
            crystals += field(equipped[i], "boost");
    for (const auto& part : state["equipped_parts"])
        if (matches(part, kind))
            parts += field(part, "boost");
    value *= timed * (1 + std::min(parts, field(data.balance, "pick_parts_boost_cap", 12)) +
                      std::min(crystals, field(data.balance, "pick_crystal_boost_cap", 20)));
    const Json& pet = state["pet_data"]["active_pet"];
    if (matches(pet, kind)) {
        const Json& cfg = data.systems["pets"];
        const int level = std::clamp(integer(pet, "level", 1), 1, std::max(1, integer(cfg, "max_level", 75)));
        const int softcap = std::max(1, integer(cfg, "growth_softcap_level", 30));
        const double effective = std::min(level - 1, softcap - 1) + std::max(0, level - softcap) * .25;
        const double percent =
            std::min(field(cfg, "active_boost_cap_pct", 650),
                     field(pet, "base_boost_pct") * (1 + effective * field(cfg, "growth_per_level", .035)));
        value *= 1 + percent / 100 * endgameMultiplier("adrenaline");
    }
    value *= 1 + std::clamp(crystalResonance().get(kind, 0).asDouble(), 0.0, .04);
    value *= 1 + mutationBonus(kind);
    return std::min(value, field(data.balance, (kind + "_effective_multiplier_cap").c_str(), 1e20));
}

double Game::rankCost(int index) const {
    if (!data.ranks.isArray() || data.ranks.empty())
        return 0;
    index = std::clamp(index, 0, static_cast<int>(data.ranks.size()) - 1);
    const double base = std::floor(currency(*this, field(data.ranks[index], "cost")));
    const int start = std::max(
        1,
        static_cast<int>(std::floor(
            data.ranks.size() * std::clamp(field(data.balance, "rank_cost_late_start_ratio", .84), 0.0, 1.0) +
            .5)));
    const double fraction = std::clamp(static_cast<double>(index + 1 - start) /
                                           std::max(1, static_cast<int>(data.ranks.size()) - start),
                                       0.0, 1.0);
    const double multiplier =
        1 + (std::max(1.0, field(data.balance, "rank_cost_late_max_mult", 1.5)) - 1) *
                std::pow(fraction, std::max(.1, field(data.balance, "rank_cost_late_curve_power", 1.05)));
    return scaledCost(*this, std::max(base, std::floor(currency(*this, base * multiplier))));
}
double Game::prestigeCost() const {
    return std::max(1.0, rankCost(static_cast<int>(data.ranks.size()) - 1));
}

Result Game::rankUp(bool maximum) {
    if (inShaft())
        return {false, "Return to your mine before ranking up."};
    if (data.ranks.empty())
        return {false, "Ranks are unavailable."};
    if (!validProgression(*this))
        return {false, "Invalid progression state."};
    const double money = field(state, "money", -1, cap(*this));
    if (money < 0 || !state["money"].isNumeric() || !std::isfinite(state["money"].asDouble()) ||
        state["money"].asDouble() > cap(*this))
        return {false, "Invalid money balance."};
    int rank = integer(state, "rank_index");
    double remaining = money;
    int count = 0;
    while (rank + 1 < static_cast<int>(data.ranks.size())) {
        const double cost = rankCost(rank + 1);
        if (remaining < cost)
            break;
        remaining = std::floor(remaining - cost);
        ++rank;
        ++count;
        if (!maximum)
            break;
    }
    if (!count)
        return {false, rank + 1 >= static_cast<int>(data.ranks.size())
                           ? "Already at the final rank."
                           : "Not enough money. Need " + compact(rankCost(rank + 1)) + "."};
    state["money"] = remaining;
    state["rank_index"] = rank;
    updateDerived(*this);
    tutorialEvent("ranked");
    return {true, "Rank " + string(state, "rank_name") + ".", "", true};
}
Result Game::prestige(bool confirmed) {
    if (inShaft())
        return {false, "Return to your mine before prestiging."};
    if (!validProgression(*this) || !validNumber(state["tokens"], cap(*this)) ||
        !validNumber(state["prestige_level"], progressLimit - 1, true) ||
        !validNumber(state["rebirth"], progressLimit - 1, true))
        return {false, "Invalid progression state."};
    if (integer(state, "rank_index") + 1 != static_cast<int>(data.ranks.size()))
        return {false, "Reach the final rank first."};
    if (!confirmed)
        return {true, "Prestige costs " + compact(prestigeCost()) + ".", "prestige_confirm"};
    const double cost = prestigeCost();
    const double money = field(state, "money", -1, cap(*this));
    if (money < cost)
        return {false, "Not enough money. Need " + compact(cost) + "."};
    const double total = field(state, "total_prestige", progressLimit, progressLimit);
    if (total >= progressLimit)
        return {false, "Prestige limit reached."};
    const int cycle = std::max(1, integer(data.balance, "prestiges_per_rebirth", 101));
    const int next = (integer(state, "prestige_level") + 1) % cycle;
    state["money"] = boolean(data.balance, "clear_money_on_prestige", true) ? 0.0 : std::floor(money - cost);
    const std::string rewardKey = std::to_string(static_cast<::Json::Int64>(total) + 1);
    state["tokens"] = std::floor(currency(
        *this, field(state, "tokens") + field(data.balance["prestige_token_rewards"], rewardKey.c_str())));
    state["total_prestige"] = total + 1;
    state["prestige_level"] = next;
    state["rebirth"] = field(state, "rebirth") + (next == 0 ? 1 : 0);
    state["rank_index"] = 0;
    updateDerived(*this);
    return {true, "Prestige P" + std::to_string(next) + ".", "", true};
}
Result Game::rebirth(bool confirmed) {
    if (integer(state, "prestige_level") !=
        std::max(1, integer(data.balance, "prestiges_per_rebirth", 101)) - 1)
        return {false, "Rebirth happens at the end of the prestige cycle."};
    return prestige(confirmed);
}

double Game::enchantCost(const std::string& id, int count) const {
    const Json& definition = data.enchants["defs"][id];
    if (!definition.isObject())
        return cap(*this);
    const int current = enchantLevel(id);
    const int limit = definition["max"].isInt() ? definition["max"].asInt() : current;
    count = std::clamp(count, 0, integer(data.enchants, "max_buy_amount", 20000));
    if (limit >= 0)
        count = std::min(count, std::max(0, limit - current));
    double total = 0;
    for (int i = 0; i < count; ++i)
        total = currency(*this, total + singleEnchantCost(*this, definition, current + i));
    return total;
}

std::string Game::enchantCurrency(const std::string& id) const {
    return string(data.enchants["defs"][id], "currency", "tokens");
}

std::string Game::currencyName(const std::string& id) const {
    return string(data.systems["currencies"][id], "label", id);
}

Json Game::makeRobot(const std::string& kind) {
    constexpr const char* kinds[] = {"money", "xp", "token"};
    Json robot = item("robot");
    const std::string normalized = rewardKind(kind);
    robot["boost_type"] = normalized.empty() ? kinds[roll(*this, 0, 2)] : normalized;
    robot["level"] = 1;
    robot["fuel"] = 0;
    robot["fuel_credit"] = 0;
    robot["uid"] = "robot-" + std::to_string(random());
    return robot;
}

double Game::robotMultiplier(const Json& robot) const {
    if (string(robot, "id") != "robot")
        return 1;
    const auto& cfg = data.systems["robots"];
    const int level = std::clamp(integer(robot, "level", 1), 1, integer(cfg, "max_level", 4000));
    return std::min(field(cfg, "multiplier_cap", 5), 1 + level * field(cfg, "boost_per_level", .001));
}

int Game::robotFuelCost(const Json& robot) const {
    const int base = 1 + (std::max(1, integer(robot, "level", 1)) - 1) /
                             std::max(1, integer(data.systems["robots"], "fuel_level_step", 1000));
    const int saving = int(std::round(mutationBonus("fuel_saving") * 100));
    return std::max(1, base - (saving ? (integer(robot, "fuel_credit", 0, 99) + base * saving) / 100 : 0));
}

Json Game::consumeRobotFuel(int blocks) {
    Json multipliers;
    constexpr const char* kinds[] = {"money", "xp", "token"};
    for (int i = 0; i < 3; ++i) {
        multipliers[kinds[i]] = 1.0;
        Json& robot = state["robot_slots"][i];
        if (blocks <= 0 || string(robot, "id") != "robot" || string(robot, "boost_type") != kinds[i])
            continue;
        const int cost = 1 + (std::max(1, integer(robot, "level", 1)) - 1) /
                                 std::max(1, integer(data.systems["robots"], "fuel_level_step", 1000));
        const int fuel = integer(robot, "fuel", 0, 1000000000);
        if (fuel <= 0)
            continue;
        const int saving = int(std::round(mutationBonus("fuel_saving") * 100));
        const int credit = saving ? integer(robot, "fuel_credit", 0, 99) : 0;
        auto charge = [&](int count) {
            const std::int64_t units = std::int64_t(count) * cost;
            return units - (credit + units * saving) / 100;
        };
        int powered = std::min(blocks, fuel / cost);
        if (saving) {
            int high = blocks;
            while (powered < high) {
                const int next = powered + (high - powered) / 2 + 1;
                // a saved fraction cannot power another block after the tank empties.
                if (charge(next) <= fuel && charge(next - 1) < fuel)
                    powered = next;
                else
                    high = next - 1;
            }
            robot["fuel_credit"] = int((credit + std::int64_t(powered) * cost * saving) % 100);
        }
        robot["fuel"] = fuel - int(charge(powered));
        multipliers[kinds[i]] = 1 + (robotMultiplier(robot) - 1) * powered / blocks;
    }
    return multipliers;
}

Json Game::robotUpgradeQuote(int index, int levels) const {
    Json quote;
    if (index < 0 || index >= 3 || string(state["robot_slots"][index], "id") != "robot") {
        quote["error"] = "Equip a robot first.";
        return quote;
    }
    const auto& cfg = data.systems["robots"];
    const Json& robot = state["robot_slots"][index];
    const int level = integer(robot, "level", 1);
    const int amount = std::min(std::clamp(levels, 1, 100), integer(cfg, "max_level", 4000) - level);
    if (amount <= 0) {
        quote["error"] = "Robot is maxed at 5x.";
        return quote;
    }
    double rubies = 0, sapphires = 0;
    for (int current = level; current < level + amount; ++current) {
        rubies +=
            field(cfg, "upgrade_ruby_base", 5) + current / std::max(1, integer(cfg, "upgrade_ruby_step", 10));
        if (current >= integer(cfg, "upgrade_sapphire_start", 100))
            sapphires += 1 + current / std::max(1, integer(cfg, "upgrade_sapphire_step", 100));
    }
    quote["index"] = index;
    quote["levels"] = amount;
    quote["robot"] = robot;
    const double multiplier = std::max(1.0, field(cfg, "upgrade_cost_multiplier", 1));
    quote["rubies"] = std::ceil(rubies * multiplier);
    quote["sapphires"] = std::ceil(sapphires * multiplier);
    return quote;
}

void Game::premiumDrops() {
    const auto triggered = [&](const char* id) {
        const auto& def = data.enchants["defs"][id];
        const auto& cfg = data.enchants["effects"][id];
        const int level = std::min(enchantLevel(id), integer(def, "max"));
        return level > 0 && integer(state, "pick_level", 1) >= integer(def, "required_pickaxe_level", 1) &&
               unit(*this) < std::min(field(cfg, "chance_cap"), level * field(cfg, "chance_per_level"));
    };
    for (const char* wallet : {"rubies", "sapphires"}) {
        const char* enchant = std::string(wallet) == "rubies" ? "ruby_greed" : "sapphire_greed";
        if (!triggered(enchant))
            continue;
        const double before = field(state, wallet);
        const double reward = std::floor(field(data.enchants["effects"][enchant], "amount", 1));
        state[wallet] = currency(*this, before + reward);
        const std::string summary = std::string("_summary_") + wallet;
        state[summary] = currency(*this, field(state, summary.c_str()) + state[wallet].asDouble() - before);
    }
    if (triggered("fuel_finder"))
        giveItem(item("robot_fuel"), true);
    const auto& cfg = data.systems["robots"];
    if (integer(state, "pick_level", 1) >= integer(cfg, "drop_pick_level", 40) &&
        unit(*this) < field(cfg, "drop_chance"))
        giveItem(makeRobot(), true);
}

void Game::awardJackpot() {
    const auto& cfg = data.systems["robots"];
    double total = 0;
    for (const auto& entry : cfg["jackpot_loot"])
        total += field(entry, "weight");
    double pick = unit(*this) * total;
    for (const auto& entry : cfg["jackpot_loot"]) {
        pick -= field(entry, "weight");
        if (pick >= 0)
            continue;
        const auto id = string(entry, "id");
        Json drop = id == "robot" ? makeRobot() : item(id.c_str());
        drop["count"] = integer(entry, "count", 1);
        if (id == "crystal" || id == "pick_part") {
            drop["tier"] =
                id == "crystal" ? crystalTier(*this) : integer(state, "parts_prestige", 0, 2147483646) + 1;
            drop["identified"] = false;
        } else if (id == "pet_egg")
            drop["tier_id"] = eggTier(data.systems["pets"]["tiers"], unit(*this));
        else if (id == "automine_time")
            drop["duration"] = integer(entry, "duration", 600);
        giveItem(drop, true);
        break;
    }
}

Result Game::buyEnchant(const std::string& id, int count) {
    const Json& definition = data.enchants["defs"][id];
    if (!definition.isObject())
        return {false, "Unknown enchant."};
    const std::string wallet = enchantCurrency(id);
    if (!validNumber(state[wallet], cap(*this)))
        return {false, "Invalid " + currencyName(wallet) + " balance."};
    const int current = enchantLevel(id);
    if (integer(state, "pick_level", 1) < integer(definition, "required_pickaxe_level", 1))
        return {false, "Requires pickaxe level " +
                           std::to_string(integer(definition, "required_pickaxe_level", 1)) + "."};
    const int limit = definition["max"].isInt() ? definition["max"].asInt() : current;
    const bool maximum = count <= 0;
    count = maximum ? integer(data.enchants, "max_buy_amount", 20000)
                    : std::min(count, integer(data.enchants, "max_buy_amount", 20000));
    if (limit >= 0)
        count = std::min(count, limit - current);
    if (count <= 0)
        return {false, "Enchant is maxed."};
    double price = 0;
    int bought = 0;
    const double balance = field(state, wallet.c_str());
    for (; bought < count; ++bought) {
        const double next = currency(*this, price + singleEnchantCost(*this, definition, current + bought));
        if (maximum && next > balance)
            break;
        price = next;
    }
    if (bought <= 0 || price > balance)
        return {false, "Not enough " + currencyName(wallet) + "."};
    state[wallet] = balance - price;
    state["enchants"][id] = current + bought;
    tutorialEvent("enchanted");
    return {true, string(definition, "label", id) + ": " + std::to_string(current + bought) + "."};
}
Result Game::prestigeParts() {
    const int required = std::min(integer(state, "parts_prestige"), 2147483646) + 1;
    for (const char* slot : {"head", "binding", "rod"}) {
        const Json& part = state["equipped_parts"][slot];
        if (!part.isObject())
            return {false, "Equip all three parts first."};
        if (integer(part, "tier", 1) != required)
            return {false, "All parts must be T" + std::to_string(required) + " to prestige."};
    }
    for (const char* slot : {"head", "binding", "rod"})
        state["equipped_parts"][slot] = Json();
    state["parts_prestige"] = required;
    return {true, "Parts prestiged. Equipped parts consumed."};
}

Json Game::captureRewardContext() const {
    Json context;
    for (const char* kind : {"money", "token", "xp"}) {
        context[std::string(kind) + "_mult"] = boost(kind);
        context[std::string("pace_") + kind] = pace(*this, kind);
    }
    context["enchants"] = state["enchants"];
    context["parts_prestige"] = state["parts_prestige"];
    context["prestige"] = state["total_prestige"];
    context["region"] = inShaft() ? state["shaft"]["floor"]["region"] : state["progression"]["mine"];
    context["contract_serial"] = state["progression"]["contract_serial"];
    context["expedition_serial"] = state["progression"]["expedition_serial"];
    context["specialization"] = specializationModifiers();
    context["surge_mult"] = endgameMultiplier("hyper_surge");
    context["overdrive_mult"] = endgameMultiplier("overdrive");
    return context;
}

void Game::awardPocketDrops(int count) {
    if (count <= 0)
        return;
    Json drop = item("crystal");
    drop["tier"] = crystalTier(*this);
    drop["identified"] = false;
    for (int i = 0; i < std::min(count, 32); ++i)
        giveItem(drop, true);
}

MiningReward Game::mineBlock(const std::string& blockId, bool allowProcs, bool manual,
                             const std::string& source, const Json& context) {
    MiningReward reward;
    if (manual && (boolean(state, "autominer_active") || shaftComplete())) {
        reward.ok = false;
        return reward;
    }
    Json current;
    if (!context.isObject() || source == "manual" || source == "drill")
        current = context.isObject() ? context : captureRewardContext();
    if (source == "manual" || source == "drill")
        current["robots"] = consumeRobotFuel(1);
    const Json& captured = current.isObject() ? current : context;
    reward = rollMiningReward(*this, blockId, source, captured, allowProcs);
    if (allowProcs)
        reward.context = captured;
    applyMiningReward(*this, reward);
    awardBlocks(*this, 1);
    const bool bulk = source == "jackhammer" || source == "veinminer" || source == "explosive";
    if (!bulk) {
        recordMining(1, blockId.size() >= 4 && blockId.compare(blockId.size() - 4, 4, "_ore") == 0 ? 1 : 0,
                     captured);
        tickItems(0, 1);
        finderDrops(*this, false, 1, captured);
    }
    if (manual && source == "manual") {
        tutorialEvent("mined");
        premiumDrops();
    }
    if (allowProcs && (!inShaft() || shaftCondition().get("procs", true) == true))
        for (const char* id : {"explosive", "jackhammer", "veinminer", "drill"}) {
            const int level = enchantLevel(id);
            if (level > 0 &&
                unit(*this) < std::min(1.0, procChance(*this, id, level) * endgameMultiplier("rage")))
                reward.procs.push_back({id, level});
        }
    if (manual && source == "manual" && allowProcs)
        rollEndgame(reward.procs);
    return reward;
}

void Game::finishProc(int virtualBlocks, int compressedBlocks, const Json& context) {
    if (virtualBlocks <= 0)
        return;
    const int compressed =
        std::clamp(compressedBlocks, 0, integer(data.balance, "proc_reward_block_cap", 250));
    const int hatchery =
        context.isObject() ? integer(context["enchants"], "hatchery") : enchantLevel("hatchery");
    tickItems(0, std::min(virtualBlocks, std::numeric_limits<int>::max() - compressed) + compressed,
              hatchery);
    finderDrops(*this, true, compressed, context);
}

void Game::awardBomb(int removed, int samples, int side, const Json& context, const Json& sample) {
    if (removed <= 0)
        return;
    removed = std::min(removed, 512);
    samples = std::clamp(samples, 0, removed);
    side = std::clamp(side, 0, std::min(samples, 250));
    Json paid = context;
    paid["robots"] = consumeRobotFuel(removed);
    MiningReward reward;
    for (int i = 0; i < samples; ++i) {
        const Json& block = sample[unit(*this) < field(sample, "ore_ratio") ? "ore" : "smooth"];
        const std::string blockId = block.isString() ? block.asString() : string(block, "id");
        const auto rolled = rollMiningReward(*this, blockId, "minebomb", paid);
        reward.money = currency(*this, reward.money + rolled.money);
        reward.tokens = currency(*this, reward.tokens + rolled.tokens);
    }
    applyMiningReward(*this, reward);
    awardBlocks(*this, removed);
    tickItems(0, side, integer(context["enchants"], "hatchery"));
    finderDrops(*this, true, side, context);
}

int Game::autominerTickCost() const {
    const int bps = std::clamp(integer(state, "autominer_bps", 3), 1, 10);
    const double step = std::max(0.0, field(data.systems["autominer"], "time_cost_per_bps_step", 2));
    return static_cast<int>(std::clamp(1 + std::ceil((bps - 1) * step), 1.0, 2147483647.0));
}

int Game::tickAutominer(double seconds, const Json& sample) {
    if (inShaft())
        return 0;
    if (!boolean(state, "autominer_active")) {
        autominerElapsed_ = 0;
        return 0;
    }
    const int selected = std::clamp(integer(state, "selected_hotbar_index"), 0, 8);
    if (string(state["inventory_slots"][selected], "id") != "pickaxe") {
        state["autominer_active"] = false;
        autominerElapsed_ = 0;
        return 0;
    }
    if (!std::isfinite(seconds) || seconds <= 0)
        return 0;
    // one bounded batch per second; stalled frames cannot create a reward backlog.
    autominerElapsed_ += std::min(seconds, 1.0);
    if (autominerElapsed_ < 1)
        return 0;
    autominerElapsed_ = std::fmod(autominerElapsed_, 1.0);
    const int cost = autominerTickCost();
    const double remaining = field(state, "autominer_time");
    if (remaining < cost) {
        state["autominer_time"] = 0.0;
        state["autominer_active"] = false;
        return 0;
    }
    if (!sample.isObject() || !sample["smooth"].isString() || !sample["ore"].isString()) {
        state["autominer_active"] = false;
        return 0;
    }
    const int bps = std::clamp(integer(state, "autominer_bps", 3), 1, 10);
    Json context = captureRewardContext();
    context["robots"] = consumeRobotFuel(bps);
    MiningReward total;
    int ores = 0;
    for (int i = 0; i < bps; ++i) {
        const bool ore = unit(*this) < field(sample, "ore_ratio", 0, 1);
        if (ore)
            ++ores;
        const auto reward =
            rollMiningReward(*this, sample[ore ? "ore" : "smooth"].asString(), "autominer", context);
        total.money += reward.money;
        total.tokens += reward.tokens;
        total.xp += reward.xp;
        finderDrops(*this, false, 1, context, true);
        tickItems(0, 1, integer(context["enchants"], "hatchery"));
    }
    applyMiningReward(*this, total);
    awardBlocks(*this, bps);
    recordMining(bps, ores, context);
    state["autominer_time"] = remaining - cost;
    if (remaining == cost)
        state["autominer_active"] = false;
    return bps;
}

void Game::tick(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    if (state.isMember("forge_slots") && state["forge_slots"].isArray()) {
        const double elapsed = field(state, "_return_items_elapsed") + seconds;
        state["_return_items_elapsed"] = std::fmod(elapsed, 1.0);
        if (elapsed >= 1)
            returnStoredItems();
    }
    tickEndgame(seconds);
    state["playtime_seconds"] = std::min(progressLimit, field(state, "playtime_seconds") + seconds);
    for (const char* kind : {"money", "token", "xp"}) {
        Json kept(::Json::arrayValue);
        for (auto row : state["boosts"][kind]) {
            if (row.isObject()) {
                row["remaining"] = std::max(0.0, field(row, "remaining") - seconds);
                if (row["remaining"].asDouble() <= 0)
                    continue;
            }
            kept.append(row);
        }
        state["boosts"][kind] = kept;
    }
    const double elapsed = field(state, "_summary_elapsed") + seconds;
    state["_summary_elapsed"] = std::fmod(elapsed, 60.0);
    if (elapsed >= 60) {
        if (boolean(state["message_settings"], "minute_summary_messages", true) &&
            (field(state, "_summary_money") > 0 || field(state, "_summary_tokens") > 0 ||
             field(state, "_summary_rubies") > 0 || field(state, "_summary_sapphires") > 0 ||
             field(state, "_summary_points") > 0)) {
            for (const char* key : {"money", "tokens", "rubies", "sapphires", "points"})
                state["mining_summary"][key] = field(state, (std::string("_summary_") + key).c_str());
        }
        for (const char* key : {"money", "tokens", "rubies", "sapphires", "points"})
            state[std::string("_summary_") + key] = 0.0;
    }
    addPickXp(*this, 0);
    tickItems(seconds, 0);
}
} // namespace prison
