#include "commands_internal.hpp"
#include "prison/game.hpp"
#include "prison/world.hpp"
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace prison {
using namespace command_detail;

namespace {
double amount(std::string text, double maximum = 1e12) {
    text = lower(text);
    double direct = numeric(text, 1, maximum);
    if (direct >= 0)
        return direct;
    if (text.size() < 2 || text.size() > 32)
        return -1;
    const std::map<char, double> factors = {{'k', 1e3}, {'m', 1e6}, {'b', 1e9}, {'t', 1e12}};
    auto found = factors.find(text.back());
    if (found == factors.end())
        return -1;
    double base = numeric(text.substr(0, text.size() - 1), 0, 1e12, false);
    double value = base * found->second;
    return value >= 1 && value <= maximum && value == std::floor(value) ? value : -1;
}
double duration(std::string text) {
    double direct = numeric(text, 1, 31536000);
    if (direct >= 0)
        return direct;
    double result = 0;
    std::string digits;
    const std::map<char, int> units = {{'d', 86400}, {'h', 3600}, {'m', 60}, {'s', 1}};
    if (text.size() > 40)
        return -1;
    for (char c : lower(text)) {
        if (c >= '0' && c <= '9')
            digits += c;
        else {
            auto unit = units.find(c);
            if (unit == units.end() || digits.empty())
                return -1;
            double value = numeric(digits, 0, 31536000);
            if (value < 0)
                return -1;
            result += value * unit->second;
            digits.clear();
        }
    }
    return digits.empty() && result >= 1 && result <= 31536000 ? result : -1;
}
std::string enchantKey(std::string key) {
    key = lower(key);
    if (key == "eff")
        return "efficiency";
    if (key == "tokengreed")
        return "token_greed";
    if (key == "rubygreed")
        return "ruby_greed";
    if (key == "sapphiregreed")
        return "sapphire_greed";
    if (key == "fuelfinder")
        return "fuel_finder";
    if (key == "hypersurge")
        return "hyper_surge";
    if (key == "creditfinder")
        return "credit_finder";
    return key;
}
std::string boostKind(std::string kind) {
    kind = lower(kind);
    kind.erase(std::remove_if(kind.begin(), kind.end(), [](unsigned char c) { return std::isspace(c); }),
               kind.end());
    if (kind == "sell" || kind == "money" || kind == "cash" || kind == "sellboost" || kind == "moneyboost" ||
        kind == "cashboost")
        return "sell";
    if (kind == "exp" || kind == "xp" || kind == "expboost" || kind == "xpboost")
        return "exp";
    if (kind == "token" || kind == "tokenboost")
        return "token";
    return "";
}
double rollTierBoost(Game& game, int tier, bool part) {
    static const double lowValues[] = {1, 5, 10, 20, 35};
    static const double highValues[] = {5, 10, 20, 35, 50};
    tier = std::max(1, tier);
    const double n = tier - 5;
    const double low = tier <= 5 ? lowValues[tier - 1] : std::floor(35 + 8 * n + 1.25 * n * n);
    const double high =
        tier <= 5 ? highValues[tier - 1] : std::max(low + 1, std::floor(50 + 11 * n + 1.55 * n * n));
    const double precision = part ? 1000.0 : 100.0;
    return std::floor(std::uniform_real_distribution<double>(low, high)(game.random) / 100 * precision + .5) /
           precision;
}
double fieldNumber(const Json& fields, const char* key, double fallback, double minimum, double maximum,
                   bool whole = true) {
    if (!fields.isMember(key))
        return fallback;
    const Json& value = fields[key];
    double parsed = 0;
    if (value.isString()) {
        if (number(value.asString(), parsed, minimum, maximum, whole))
            return parsed;
    } else if (value.isNumeric()) {
        parsed = value.asDouble();
        if (std::isfinite(parsed) && parsed >= minimum && parsed <= maximum &&
            (!whole || parsed == std::floor(parsed)))
            return parsed;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
std::string fieldString(const Json& fields, const char* key, const std::string& fallback = "") {
    return fields.isMember(key) && fields[key].isString() ? fields[key].asString() : fallback;
}
std::string randomPetRarity(Game& game) {
    std::vector<double> weights;
    for (const auto& row : game.data.systems["pets"]["tiers"])
        weights.push_back(row.get("hatchable", true).asBool() ? row.get("weight", 0).asDouble() : 0.0);
    std::discrete_distribution<unsigned> choose(weights.begin(), weights.end());
    return game.data.systems["pets"]["tiers"][choose(game.random)]["id"].asString();
}
Json makeTestPet(Game& game, const Json& fields, const std::string& rarity) {
    const Json& cfg = game.data.systems["pets"];
    Json tier;
    for (const auto& row : cfg["tiers"])
        if (row["id"].asString() == rarity) {
            tier = row;
            break;
        }
    Json pet;
    pet["id"] = "pet";
    pet["count"] = 1;
    pet["tier_id"] = rarity;
    const char* kinds[] = {"sell", "token", "exp"};
    pet["boost_type"] = fields.isMember("boost_type") ? fields["boost_type"] : Json(kinds[game.random() % 3]);
    pet["base_boost_pct"] = fields.isMember("percent")
                                ? fields["percent"].asDouble()
                                : std::round(std::uniform_real_distribution<double>(
                                                 tier.get("boost_min", 1).asDouble(),
                                                 tier.get("boost_max", 5).asDouble())(game.random) *
                                             100) /
                                      100;
    pet["mood"] = cfg["moods"][static_cast<unsigned>(game.random() % cfg["moods"].size())];
    pet["pet_type"] = fields.isMember("pet_type")
                          ? fields["pet_type"]
                          : cfg["types"][static_cast<unsigned>(game.random() % cfg["types"].size())];
    std::ostringstream color;
    color << '#' << std::hex << std::setfill('0');
    for (int i = 0; i < 3; ++i)
        color << std::setw(2) << (70 + game.random() % 166);
    pet["pet_color"] =
        tier.get("merge_only", false).asBool() ? tier.get("color", "#71ea5b").asString() : color.str();
    pet["level"] = fields.get("level", 1).asInt();
    pet["exp"] = 0;
    pet["food_value"] = tier.get("food_value", 1);
    pet["last_fed_ts"] = 0;
    pet["uid"] = "dev-pet-" + std::to_string(game.random());
    pet["mutation_rolled"] = true;
    if (fields.get("mutation", "none") != "none") {
        pet["mutation"] = fields["mutation"];
        pet["pet_color"] = game.petMutation(pet)["color"];
    }
    return pet;
}
const std::map<std::string, std::string> usage = {
    {"test", "/test [mine [max|normal|0-50]|guardian|stress <mine|resetmine|rankup|prestige|stop|status>]"},
    {"givecrystal", "/givecrystal <tier> [count] [sell|token|exp] [percent]"},
    {"givedust", "/givedust <tier> [count]"},
    {"givepart", "/givepart <sell|token|exp> <tier> [head|binding|rod] [percent] [count]"},
    {"giveegg", "/giveegg [count] [rarity]"},
    {"givepet", "/givepet <rarity|type> [type|rarity]"},
    {"givebook", "/givebook <enchant> <count>"},
    {"givexp", "/givexp <EXP> <count>"},
    {"givetime", "/givetime <duration> [count]"},
    {"givemultiplier", "/givemultiplier <multiplier> <duration> [count]"},
    {"givefood", "/givefood [count]"},
    {"giverobot", "/giverobot <sell|token|exp> [level]"},
    {"givefuel", "/givefuel [count]"},
    {"giverubies", "/giverubies <amount>"},
    {"givesapphires", "/givesapphires <amount>"},
    {"grantrename", "/grantrename [count]"},
    {"givebomb", "/givebomb <tier> <count>"},
    {"givepouch", "/givepouch <small|medium|large> [count]"},
    {"picklevel", "/picklevel [give|take|set] <amount>"},
    {"pickprestige", "/pickprestige <1-1000>"},
    {"pickname", "/pickname <name>"},
    {"pickenchant", "/pickenchant <enchant> <level>"}};
const std::map<std::string, std::pair<int, int>> arities = {
    {"giverobot", {1, 2}},      {"givefuel", {0, 1}},  {"giverubies", {1, 1}},   {"givesapphires", {1, 1}},
    {"givecrystal", {1, 4}},    {"givedust", {1, 2}},  {"givepart", {2, 5}},     {"giveegg", {0, 2}},
    {"givepet", {1, 2}},        {"givebook", {2, 2}},  {"givexp", {2, 2}},       {"givetime", {1, 2}},
    {"givemultiplier", {2, 3}}, {"givefood", {0, 1}},  {"grantrename", {0, 1}},  {"givebomb", {2, 2}},
    {"givepouch", {1, 2}},      {"picklevel", {1, 2}}, {"pickprestige", {1, 1}}, {"pickname", {1, 48}},
    {"pickenchant", {2, 2}}};
} // namespace

Result Game::developmentCommand(const std::string& name, const std::vector<std::string>& args) {
    if (!usage.count(name))
        return error("Unknown command. Use /help.");
    if (!state.get("devmode", false).asBool())
        return error("This command requires a world created with Devmode on.");
    if (arities.count(name)) {
        const auto bounds = arities.at(name);
        if (int(args.size()) < bounds.first || int(args.size()) > bounds.second)
            return error("Usage: " + usage.at(name));
    }
    if (name == "giverubies" || name == "givesapphires") {
        const double maximum = data.balance.get("currency_cap", 9e33).asDouble();
        const double count = amount(args[0], maximum);
        if (!std::isfinite(count) || count < 1 || count > maximum || std::floor(count) != count)
            return error("Enter a whole amount from 1 to " + compact(maximum) + ".");
        const std::string wallet = name == "giverubies" ? "rubies" : "sapphires";
        state[wallet] = std::min(maximum, state[wallet].asDouble() + count);
        return ok("Added " + compact(count) + " " + currencyName(wallet) + ".");
    }
    if (name == "test") {
        if (args.empty())
            return {true, "", "test"};
        if (lower(args[0]) == "mine" && args.size() <= 2) {
            if (args.size() == 1)
                return {true, "", "test:mine"};
            if (inShaft())
                return error("Leave the shaft before changing mine size.");
            const auto value = lower(args[1]);
            double prestige = MineSizePrestigeCap;
            if (value != "max" && value != "normal" && !number(value, prestige, 0, MineSizePrestigeCap))
                return error("Usage: /test mine [max|normal|0-50]");
            if (value == "normal")
                state.removeMember("dev_mine_prestige");
            else
                state["dev_mine_prestige"] = int(prestige);
            state["_stress"]["active"] = false;
            state["autominer_active"] = false;
            return {true,
                    value == "normal" ? "Mine size follows your prestige again."
                                      : "Mine size set to prestige " + std::to_string(int(prestige)) + ".",
                    "test:mine:apply", true};
        }
        if (args.size() == 1 && lower(args[0]) == "guardian")
            return {true, "", "guardian:summon"};
        if (args.size() == 2 && lower(args[0]) == "stress") {
            const auto kind = lower(args[1]);
            if (kind == "mine" || kind == "resetmine" || kind == "rankup" || kind == "prestige") {
                if (inShaft())
                    return error("Leave the shaft before starting a stress test.");
                state["_stress"]["type"] = kind;
                state["_stress"]["active"] = true;
                state["_stress"]["iterations"] = 0;
                return {true, "Stress test started: " + kind, "stress:" + kind};
            }
            if (kind == "stop") {
                state["_stress"]["active"] = false;
                return ok("Stress test stopped.");
            }
            if (kind == "status")
                return ok(state["_stress"].get("active", false).asBool()
                              ? "Stress: " + state["_stress"].get("type", "").asString() + ", " +
                                    compact(state["_stress"].get("iterations", 0).asDouble()) + " iterations"
                              : "No stress test running.");
        }
        if (args.size() == 1 && args[0] == "funds") {
            state["money"] = std::max(state.get("money", 0).asDouble(), 1e30);
            state["tokens"] = std::max(state.get("tokens", 0).asDouble(), 1e30);
            state["rubies"] = std::max(state.get("rubies", 0).asDouble(), 1e24);
            state["sapphires"] = std::max(state.get("sapphires", 0).asDouble(), 1e24);
            state["points"] = std::max(state.get("points", 0).asDouble(), 100000.0);
            state["autominer_time"] = std::max(state.get("autominer_time", 0).asDouble(), 86400.0);
            return ok("Test funds refilled.");
        }
        if (args.size() == 1 && args[0] == "max") {
            Json oldSlots = state["inventory_slots"], oldParts = state["equipped_parts"],
                 oldCrystals = state["equipped_crystals"];
            for (const auto& part : oldParts)
                if (part.isObject() && !giveItem(part)) {
                    state["inventory_slots"] = oldSlots;
                    return error("Free inventory space before replacing equipment.");
                }
            for (const auto& crystal : oldCrystals)
                if (crystal.isObject() && !giveItem(crystal)) {
                    state["inventory_slots"] = oldSlots;
                    return error("Free inventory space before replacing equipment.");
                }
            state["pick_level"] = std::max(5000, state.get("pick_level", 1).asInt());
            state["pick_xp_needed"] = pickXpNeeded(state["pick_level"].asInt());
            state["parts_prestige"] = std::max(9, state.get("parts_prestige", 0).asInt());
            for (const auto& key : data.enchants["defs"].getMemberNames()) {
                int max = data.enchants["defs"][key].get("max", 0).asInt();
                state["enchants"][key] = max >= 0 ? max : 10000;
            }
            const char* partNames[] = {"head", "binding", "rod"};
            const char* kinds[] = {"sell", "token", "exp"};
            for (int i = 0; i < 3; ++i) {
                Json part;
                part["id"] = "pick_part";
                part["tier"] = state["parts_prestige"].asInt() + 1;
                part["count"] = 1;
                part["identified"] = true;
                part["slot"] = partNames[i];
                part["boost_type"] = kinds[i];
                part["boost"] = 1.43;
                state["equipped_parts"][partNames[i]] = part;
            }
            state["equipped_crystals"] = Json(::Json::arrayValue);
            for (int i = 0; i < crystalSlots(); ++i) {
                Json crystal;
                crystal["id"] = "crystal";
                crystal["tier"] = 10;
                crystal["count"] = 1;
                crystal["identified"] = true;
                crystal["boost_type"] = kinds[i % 3];
                crystal["boost"] = 1.43;
                state["equipped_crystals"].append(crystal);
            }
            return ok("Test pickaxe equipped.");
        }
        if (args.size() == 1 && args[0] == "kit") {
            const Json before = state["inventory_slots"];
            for (const auto* command : {"/givecrystal 10 8",
                                        "/givedust 10 16",
                                        "/givepart sell 10 head 100 1",
                                        "/givepart token 10 binding 100 1",
                                        "/givepart exp 10 rod 100 1",
                                        "/givefood 32",
                                        "/givexp 1m 4",
                                        "/givebook experienced 4",
                                        "/givetime 1h 8",
                                        "/givebomb 1 8",
                                        "/givepouch small",
                                        "/givemultiplier 2 30m 4",
                                        "/giveegg 4 common",
                                        "/giveegg 4 uncommon",
                                        "/giveegg 4 rare",
                                        "/givepet common",
                                        "/giverobot sell",
                                        "/giverobot exp",
                                        "/giverobot token",
                                        "/givefuel 64"}) {
                Result result = this->command(command);
                if (!result.ok) {
                    state["inventory_slots"] = before;
                    return error("Free inventory space for the test kit.");
                }
            }
            return ok("Test kit added.");
        }
        return error("Usage: " + usage.at(name));
    }
    if (name == "pickname") {
        Json fields;
        fields["name"] = join(args);
        return customizeTestPick(fields);
    }
    if (name == "picklevel") {
        double value = amount(args.back());
        if (value < 1)
            return error("Invalid level.");
        if (args.size() == 2) {
            const auto operation = lower(args[0]);
            if (operation == "give")
                value += state.get("pick_level", 1).asInt();
            else if (operation == "take")
                value = std::max(1.0, state.get("pick_level", 1).asInt() - value);
            else if (operation != "set")
                return error("Usage: " + usage.at(name));
        }
        Json fields;
        fields["level"] = value;
        return customizeTestPick(fields);
    }
    if (name == "pickprestige") {
        Json fields;
        fields["prestige"] = args[0];
        return customizeTestPick(fields);
    }
    if (name == "pickenchant") {
        Json fields;
        fields["enchant"] = args[0];
        fields["enchant_level"] = args[1];
        return customizeTestPick(fields);
    }
    Json fields;
    if (name == "givecrystal" || name == "givedust") {
        fields["id"] = name == "givecrystal" ? "crystal" : "crystal_dust";
        fields["tier"] = args[0];
        if (args.size() > 1)
            fields["count"] = args[1];
        if (args.size() > 2) {
            fields["identified"] = true;
            fields["boost_type"] = args[2];
        }
        if (args.size() > 3)
            fields["percent"] = args[3];
    } else if (name == "givepart") {
        fields["id"] = "pick_part";
        fields["identified"] = true;
        fields["boost_type"] = args[0];
        fields["tier"] = args[1];
        if (args.size() > 2)
            fields["slot"] = args[2];
        if (args.size() > 3)
            fields["percent"] = args[3];
        if (args.size() > 4)
            fields["count"] = args[4];
    } else if (name == "giveegg") {
        fields["id"] = "pet_egg";
        fields["count"] = args.empty() ? Json(1) : Json(args[0]);
        fields["rarity"] = args.size() > 1 ? args[1] : "random";
    } else if (name == "givepet") {
        fields["id"] = "pet";
        fields["rarity"] = "random";
        for (const auto& token : args) {
            bool found = false;
            for (const auto& tier : data.systems["pets"]["tiers"])
                if (lower(token) == tier["id"].asString()) {
                    fields["rarity"] = lower(token);
                    found = true;
                    break;
                }
            if (!found)
                for (const auto& type : data.systems["pets"]["types"])
                    if (lower(token) == lower(type.asString())) {
                        fields["pet_type"] = type;
                        found = true;
                        break;
                    }
            if (!found)
                return error("Unknown pet rarity or type.");
        }
    } else if (name == "givebook") {
        fields["id"] = "enchant_book";
        fields["enchant"] = args[0];
        fields["count"] = args[1];
    } else if (name == "givexp") {
        fields["id"] = "liquid_xp";
        fields["amount"] = amount(args[0]);
        fields["count"] = args[1];
    } else if (name == "givetime") {
        fields["id"] = "automine_time";
        fields["duration"] = duration(args[0]);
        if (args.size() > 1)
            fields["count"] = args[1];
    } else if (name == "givemultiplier") {
        fields["id"] = "multiplier";
        fields["amount"] = args[0];
        fields["duration"] = duration(args[1]);
        if (args.size() > 2)
            fields["count"] = args[2];
    } else if (name == "giverobot") {
        fields["id"] = "robot";
        fields["boost_type"] = args[0];
        if (args.size() > 1)
            fields["level"] = args[1];
    } else if (name == "givefood" || name == "grantrename" || name == "givefuel") {
        fields["id"] = name == "givefood" ? "pet_food" : name == "givefuel" ? "robot_fuel" : "rename_token";
        if (!args.empty())
            fields["count"] = args[0];
    } else if (name == "givebomb") {
        fields["id"] = "minebomb";
        auto tier = lower(args[0]);
        if (!tier.empty() && tier[0] == 't')
            tier.erase(0, 1);
        fields["tier"] = tier;
        fields["count"] = args[1];
    } else if (name == "givepouch") {
        fields["id"] = "pouch";
        fields["size"] = args[0];
        if (args.size() > 1)
            fields["count"] = args[1];
    } else
        return error("Unknown development command.");
    return giveTestItem(fields);
}

Result Game::giveTestItem(const Json& fields) {
    if (!state["devmode"].isBool() || !state["devmode"].asBool())
        return error("Item grants require Devmode.");
    if (!fields.isObject())
        return error("Choose a supported item.");
    static const std::set<std::string> knownFields = {
        "id",    "count",    "tier",    "identified", "boost_type", "percent", "slot",    "rarity",
        "level", "pet_type", "enchant", "amount",     "duration",   "size",    "mutation"};
    for (const auto& key : fields.getMemberNames())
        if (!knownFields.count(key))
            return error("Unknown item setting: " + key);
    const std::string id = fieldString(fields, "id");
    static const std::set<std::string> ids = {"crystal",       "crystal_dust", "pick_part",    "pet_egg",
                                              "pet",           "pet_food",     "enchant_book", "liquid_xp",
                                              "automine_time", "multiplier",   "minebomb",     "pouch",
                                              "rename_token",  "robot",        "robot_fuel"};
    if (!ids.count(id))
        return error("Choose a supported item.");
    Json checked = fields;
    const double quantity = fieldNumber(fields, "count", 1, 1, 99);
    if (!std::isfinite(quantity))
        return error("Count must be 1-99.");
    checked["count"] = static_cast<int>(quantity);
    if (id == "robot") {
        const double level =
            fieldNumber(fields, "level", 1, 1, data.systems["robots"].get("max_level", 4000).asInt());
        if (!std::isfinite(level))
            return error("Robot level must be 1-4000.");
        checked["level"] = static_cast<int>(level);
    }
    if (id == "crystal" || id == "crystal_dust" || id == "pick_part" || id == "minebomb") {
        const double tier = fieldNumber(fields, "tier", 1, 1, id == "minebomb" ? 20 : 1000);
        if (!std::isfinite(tier))
            return error(id == "minebomb" ? "Bomb tier must be 1-20." : "Item tier must be 1-1000.");
        checked["tier"] = static_cast<int>(tier);
    }
    if (fields.isMember("identified") && !fields["identified"].isBool())
        return error("Identified must be on or off.");
    if (fields.isMember("boost_type")) {
        const auto kind = boostKind(fieldString(fields, "boost_type"));
        if (kind.empty())
            return error("Choose Sell, Token or EXP.");
        checked["boost_type"] = kind;
    }
    if (fields.isMember("percent")) {
        const double percent = fieldNumber(fields, "percent", 0, 0, 1000000, false);
        if (!std::isfinite(percent))
            return error("Boost must be 0-1000000 percent.");
        checked["percent"] = percent;
    }
    if (fields.isMember("slot")) {
        const auto slot = lower(fieldString(fields, "slot"));
        if (slot != "head" && slot != "binding" && slot != "rod")
            return error("Choose Head, Binding or Rod.");
        checked["slot"] = slot;
    }
    if (id == "pet" || id == "pet_egg") {
        const auto rarity = lower(fieldString(fields, "rarity", "common"));
        if (fields.isMember("rarity") && !fields["rarity"].isString())
            return error("Unknown pet rarity.");
        bool valid = rarity == "random";
        for (const auto& tier : data.systems["pets"]["tiers"])
            if (tier["id"].asString() == rarity && (id == "pet" || tier.get("hatchable", true).asBool()))
                valid = true;
        if (!valid)
            return error("Unknown pet rarity.");
        checked["rarity"] = rarity;
        const auto mutation = fieldString(fields, "mutation", "none");
        if (mutation != "none") {
            Json candidate;
            candidate["id"] = "pet";
            candidate["mutation"] = mutation;
            if (id != "pet" || !petMutation(candidate).isObject())
                return error("Choose a known pet mutation.");
        }
        checked["mutation"] = mutation;
        const double level =
            fieldNumber(fields, "level", 1, 1, data.systems["pets"].get("max_level", 75).asInt());
        if (!std::isfinite(level))
            return error("Invalid pet level.");
        checked["level"] = static_cast<int>(level);
        if (fields.isMember("pet_type")) {
            valid = false;
            for (const auto& type : data.systems["pets"]["types"])
                if (lower(type.asString()) == lower(fieldString(fields, "pet_type"))) {
                    checked["pet_type"] = type;
                    valid = true;
                    break;
                }
            if (!valid)
                return error("Unknown pet type.");
        }
    }
    if (id == "enchant_book") {
        const auto key = enchantKey(fieldString(fields, "enchant"));
        if (!data.enchants["defs"].isMember(key))
            return error("Unknown enchant.");
        if (enchantCurrency(key) != "tokens")
            return error("This enchant requires " + currencyName(enchantCurrency(key)) +
                         ". Use /pickenchant in Devmode.");
        checked["enchant"] = key;
        const double levels = fieldNumber(fields, "amount", 1, 1, 10000);
        if (!std::isfinite(levels))
            return error("Book levels must be 1-10000.");
        checked["amount"] = static_cast<int>(levels);
    }
    if (id == "liquid_xp") {
        const double xp = fieldNumber(fields, "amount", 1, 1, 1e12);
        if (!std::isfinite(xp))
            return error("EXP must be 1-1000000000000.");
        checked["amount"] = xp;
    }
    if (id == "multiplier") {
        const double multiplier = fieldNumber(fields, "amount", 2, 1, 100, false);
        if (!std::isfinite(multiplier))
            return error("Multiplier must be 1-100.");
        checked["amount"] = multiplier;
    }
    if (id == "multiplier" || id == "automine_time") {
        const double seconds = fieldNumber(fields, "duration", 3600, 1, 31536000);
        if (!std::isfinite(seconds))
            return error("Duration must be 1 second to 365 days.");
        checked["duration"] = seconds;
    }
    if (id == "pouch") {
        auto size = lower(fieldString(fields, "size", "small"));
        if (size == "1")
            size = "small";
        if (size == "2")
            size = "medium";
        if (size == "3")
            size = "large";
        if ((fields.isMember("size") && !fields["size"].isString()) ||
            !data.systems["pouch_slots"].isMember(size))
            return error("Choose small, medium or large.");
        checked["size"] = size;
    }
    // every grant uses the ordinary manual delivery rules. roll back the entire
    // transaction (including item rolls) if even the last item cannot fit.
    const Json before = state["inventory_slots"];
    const auto previousRandom = random;
    for (int index = 0; index < static_cast<int>(quantity); ++index) {
        Json item;
        item["id"] = id;
        item["count"] = 1;
        if (id == "crystal" || id == "crystal_dust" || id == "pick_part" || id == "minebomb")
            item["tier"] = checked["tier"];
        if (id == "crystal" || id == "pick_part") {
            item["identified"] = checked.get("identified", false);
            if (id == "pick_part") {
                const char* slots[] = {"head", "binding", "rod"};
                item["slot"] = checked.isMember("slot") ? checked["slot"] : Json(slots[random() % 3]);
            }
            if (item["identified"].asBool()) {
                item["boost_type"] = checked.get("boost_type", "sell");
                const double precision = id == "pick_part" ? 1000 : 100;
                item["boost"] =
                    checked.isMember("percent")
                        ? std::floor(checked["percent"].asDouble() / 100 * precision + .5) / precision
                        : rollTierBoost(*this, item["tier"].asInt(), id == "pick_part");
                item["uid"] = "dev-" + id + "-" + std::to_string(random());
            }
        }
        if (id == "pet" || id == "pet_egg") {
            const auto rarity = checked["rarity"].asString() == "random" ? randomPetRarity(*this)
                                                                         : checked["rarity"].asString();
            if (id == "pet")
                item = makeTestPet(*this, checked, rarity);
            else
                item["tier_id"] = rarity;
        }
        if (id == "robot") {
            item = makeRobot(checked.get("boost_type", "sell").asString());
            item["level"] = checked["level"];
        }
        if (id == "enchant_book") {
            item["enchant_key"] = checked["enchant"];
            item["amount"] = checked["amount"];
        }
        if (id == "liquid_xp")
            item["amount"] = checked["amount"];
        if (id == "multiplier") {
            item["amount"] = checked["amount"];
            item["boost_type"] = checked.get("boost_type", "sell");
        }
        if (id == "multiplier" || id == "automine_time")
            item["duration"] = checked["duration"];
        if (id == "pouch") {
            item["tier"] = checked["size"];
            item["slots"] = data.systems["pouch_slots"][checked["size"].asString()];
            item["items"] = Json(::Json::arrayValue);
        }
        if (!giveItem(item, false)) {
            state["inventory_slots"] = before;
            random = previousRandom;
            return error("Inventory full. Nothing was granted.");
        }
    }
    auto label = id;
    std::replace(label.begin(), label.end(), '_', ' ');
    return ok("Added " + std::to_string(static_cast<int>(quantity)) + " " + label + ".");
}

Result Game::customizeTestPick(const Json& fields) {
    if (!state["devmode"].isBool() || !state["devmode"].asBool())
        return error("Pickaxe customization requires Devmode.");
    if (!fields.isObject() || fields.empty())
        return error("Choose a pickaxe setting.");
    static const std::set<std::string> keys = {"name", "level", "prestige", "enchant", "enchant_level"};
    for (const auto& key : fields.getMemberNames())
        if (!keys.count(key))
            return error("Unknown pickaxe setting: " + key);
    std::string name;
    if (fields.isMember("name")) {
        if (!fields["name"].isString())
            return error("Name must be 1-48 characters.");
        name = fields["name"].asString();
        if (name.size() > 48)
            return error("Name must be 1-48 characters.");
        for (unsigned char c : name)
            if (c < 32 || c == 127)
                return error("Name must be one line.");
        const auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
        if (first == std::string::npos)
            return error("Name must be 1-48 characters.");
        name = name.substr(first, last - first + 1);
    }
    const double level = fieldNumber(fields, "level", 1, 1, 10000);
    const double prestige = fieldNumber(fields, "prestige", 1, 1, 1000);
    if (!std::isfinite(level))
        return error("Pick level must be 1-10000.");
    if (!std::isfinite(prestige))
        return error("Parts prestige must be P1-P1000.");
    std::string enchant;
    double enchantLevel = 0;
    if (fields.isMember("enchant") || fields.isMember("enchant_level")) {
        enchant = enchantKey(fieldString(fields, "enchant"));
        if (!data.enchants["defs"].isMember(enchant))
            return error("Unknown enchant.");
        const int cap = data.enchants["defs"][enchant].get("max", 0).asInt();
        enchantLevel = fieldNumber(fields, "enchant_level", -1, 0, cap >= 0 ? cap : 100000);
        if (!std::isfinite(enchantLevel) || enchantLevel < 0)
            return error("Enchant level exceeds its test limit.");
    }
    const Json inventory = state["inventory_slots"];
    Json parts = state["equipped_parts"];
    if (fields.isMember("prestige")) {
        for (const auto& slot : parts.getMemberNames()) {
            if (parts[slot].isObject() && parts[slot].get("tier", 1).asInt() > static_cast<int>(prestige)) {
                if (!giveItem(parts[slot], false)) {
                    state["inventory_slots"] = inventory;
                    return error("Free inventory space before lowering parts prestige.");
                }
                parts[slot] = Json();
            }
        }
    }
    // no progression or equipment fields change until all returned parts fit.
    state["equipped_parts"] = parts;
    if (fields.isMember("name"))
        state["pick_custom_name"] = name;
    if (fields.isMember("level") && static_cast<int>(level) != state.get("pick_level", 1).asInt()) {
        state["pick_level"] = static_cast<int>(level);
        state["pick_xp"] = 0;
        state["pick_xp_needed"] = pickXpNeeded(static_cast<int>(level));
        state["crystal_slots"] = crystalSlots();
    }
    if (fields.isMember("prestige"))
        state["parts_prestige"] = static_cast<int>(prestige) - 1;
    if (!enchant.empty()) {
        if (enchantLevel == 0)
            state["enchants"].removeMember(enchant);
        else
            state["enchants"][enchant] = static_cast<int>(enchantLevel);
    }
    return ok("Pickaxe updated.");
}

} // namespace prison
