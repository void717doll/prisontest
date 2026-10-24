#include "items_internal.hpp"
#include "prison/lore.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace prison {
using namespace item_detail;

namespace {
std::string displayKind(std::string value) {
    value = rewardKind(value);
    if (value == "money")
        return "Sell";
    if (value == "xp")
        return "EXP";
    return "Token";
}

const Json& petTier(const Game& game, const std::string& tier) {
    for (const auto& row : game.data.systems["pets"]["tiers"])
        if (row.get("id", "").asString() == tier)
            return row;
    return game.data.systems["pets"]["tiers"][0];
}

double randomRange(Game& game, double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(game.random);
}

double rollBoost(Game& game, int tier, const std::string& family) {
    static const double lo[] = {1, 5, 10, 20, 35}, hi[] = {5, 10, 20, 35, 50};
    tier = std::max(1, tier);
    double low, high;
    if (tier <= 5) {
        low = lo[tier - 1];
        high = hi[tier - 1];
    } else {
        double n = tier - 5;
        low = std::floor(35 + 8 * n + 1.25 * n * n);
        high = std::max(low + 1, std::floor(50 + 11 * n + 1.55 * n * n));
    }
    const double precision = family == "pick_part" ? 1000.0 : 100.0;
    return std::floor(randomRange(game, low, high) / 100 * precision + 0.5) / precision;
}

Json rollPet(Game& game, const std::string& rarity) {
    const Json& tier = petTier(game, rarity);
    const Json& cfg = game.data.systems["pets"];
    Json pet(::Json::objectValue);
    pet["id"] = "pet";
    pet["count"] = 1;
    pet["tier_id"] = tier.get("id", "common");
    const char* kinds[] = {"sell", "token", "exp"};
    pet["boost_type"] = kinds[game.random() % 3];
    pet["base_boost_pct"] = std::round(randomRange(game, tier.get("boost_min", 1).asDouble(),
                                                   tier.get("boost_max", 5).asDouble()) *
                                       100) /
                            100;
    pet["mood"] = cfg["moods"].empty()
                      ? Json("Happy")
                      : cfg["moods"][static_cast<unsigned>(game.random() % cfg["moods"].size())];
    pet["pet_type"] = cfg["types"].empty()
                          ? Json("Rabbit")
                          : cfg["types"][static_cast<unsigned>(game.random() % cfg["types"].size())];
    std::ostringstream color;
    color << '#' << std::hex << std::setfill('0');
    for (int i = 0; i < 3; i++)
        color << std::setw(2) << (70 + game.random() % 166);
    pet["pet_color"] =
        tier.get("merge_only", false).asBool() ? tier.get("color", "#71ea5b").asString() : color.str();
    pet["level"] = 1;
    pet["exp"] = 0;
    pet["food_value"] = tier.get("food_value", 1);
    pet["last_fed_ts"] = 0;
    pet["uid"] = uid(game, "pet");
    return pet;
}

void addXp(Game& game, double amount) {
    if (!std::isfinite(amount) || amount <= 0)
        return;
    double xp = std::min(game.data.balance.get("currency_cap", 9e33).asDouble(),
                         game.state.get("pick_xp", 0).asDouble() + amount);
    int level = std::max(1, game.state.get("pick_level", 1).asInt());
    for (int steps = 0;
         steps < 32 && level < std::numeric_limits<int>::max() && xp >= game.pickXpNeeded(level); ++steps) {
        xp -= game.pickXpNeeded(level);
        ++level;
    }
    game.state["pick_level"] = level;
    game.state["pick_xp"] = xp;
    game.state["pick_xp_needed"] = game.pickXpNeeded(level);
}

std::string title(std::string name) {
    if (!name.empty())
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::string percent(double boost) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << boost;
    return text.str();
}
} // namespace

namespace item_detail {
Result fail(const std::string& message) {
    return {false, message, {}};
}

Result success(const std::string& message) {
    return {true, message, {}};
}

bool apply(Game& game, const Json& item) {
    const auto name = id(item);
    const int total = count(item);
    if (name == "automine_time") {
        const double remaining = game.state.get("autominer_time", 0).asDouble();
        const double added = std::max(1.0, item.get("duration", 3600).asDouble()) * total;
        game.state["autominer_time"] = remaining + added;
        if (game.state.get("autominer_active", false).asBool())
            game.state["_autominer_duration"] =
                std::max(remaining, game.state.get("_autominer_duration", remaining).asDouble()) + added;
        return true;
    }
    if (name == "liquid_xp") {
        double amount = item.get("amount", item.get("xp", 0)).asDouble() * total;
        if (!std::isfinite(amount) || amount <= 0)
            return false;
        addXp(game, amount);
        return true;
    }
    if (name == "enchant_book") {
        std::string key = item.get("enchant_key", "").asString();
        const auto& defs = game.data.enchants["defs"];
        if (!defs.isMember(key)) {
            std::vector<std::string> names;
            for (const auto& candidate : defs.getMemberNames())
                if (game.enchantCurrency(candidate) == "tokens")
                    names.push_back(candidate);
            if (names.empty())
                return false;
            key = names[game.random() % names.size()];
        }
        if (game.enchantCurrency(key) != "tokens")
            return false;
        game.state["enchants"][key] =
            std::min(1000000000.0, game.state["enchants"].get(key, 0).asDouble() +
                                       std::max(1, item.get("amount", 1).asInt()) * double(total));
        return true;
    }
    if (name == "multiplier") {
        std::string kind = rewardKind(item.get("boost_type", "sell").asString());
        if (kind != "money" && kind != "token" && kind != "xp")
            return false;
        double amount = item.get("amount", 1.5).asDouble(),
               duration = item.get("duration", item.get("remaining", 1800)).asDouble() * total;
        if (!std::isfinite(amount) || !std::isfinite(duration) || amount < 1 || duration <= 0)
            return false;
        Json& rows = game.state["boosts"][kind];
        if (!rows.isArray())
            rows = Json(::Json::arrayValue);
        for (auto& row : rows)
            if (row.isObject() && std::abs(row.get("amount", 1).asDouble() - amount) < 0.001) {
                const double remaining = std::max(0.0, row.get("remaining", 0).asDouble());
                row["duration"] = std::max(remaining, row.get("duration", remaining).asDouble()) + duration;
                row["remaining"] = remaining + duration;
                return true;
            }
        if (rows.size() >= 256)
            return false;
        Json row(::Json::objectValue);
        row["amount"] = amount;
        row["remaining"] = duration;
        row["duration"] = duration;
        rows.append(row);
        return true;
    }
    return false;
}

bool salvage(Game& game, const Json& item) {
    const auto& cfg = game.data.systems["salvage"];
    bool allowed = false;
    for (const auto& name : cfg["eligible_items"])
        if (name.asString() == id(item))
            allowed = true;
    if (!allowed)
        return false;
    int tier = item.get("tier", 0).isNumeric() ? item.get("tier", 0).asInt() : 0;
    if (item.isMember("tier_id")) {
        int i = 0;
        for (const auto& row : game.data.systems["pets"]["tiers"]) {
            ++i;
            if (row["id"] == item["tier_id"])
                tier = i;
        }
    }
    tier = std::clamp(tier, 0, cfg.get("salvage_tier_cap", 25).asInt());
    double low = cfg.get("base_min", 500).asDouble(), high = cfg.get("base_max", 2500).asDouble();
    const auto& ranges = cfg["tier_ranges"];
    if (tier > 0 && !ranges.empty()) {
        unsigned n = std::min(static_cast<unsigned>(tier - 1), ranges.size() - 1);
        low = ranges[n][0].asDouble();
        high = ranges[n][1].asDouble();
        if (tier > int(ranges.size())) {
            double extra = tier - ranges.size();
            low = std::floor(low * (1 + extra * .40));
            high = std::floor(high * (1 + extra * .55));
        }
    }
    double amount = std::min(cfg.get("max_tokens_per_redeem", 500000000).asDouble(),
                             std::floor(randomRange(game, low, high + 1)) * count(item));
    game.state["tokens"] = std::min(game.data.balance.get("currency_cap", 9e33).asDouble(),
                                    game.state.get("tokens", 0).asDouble() + amount);
    return true;
}
} // namespace item_detail

const Json& Game::pointShopProducts() const {
    static const Json products = [] {
        struct Product {
            const char* key;
            const char* name;
            int price;
            const char* item;
            const char* tier;
            double amount;
            int duration;
        };
        const Product rows[] = {{"pet_egg", "Common Egg", 1, "pet_egg", "common", 0, 0},
                                {"uncommon_egg", "Uncommon Egg", 3, "pet_egg", "uncommon", 0, 0},
                                {"rare_egg", "Rare Egg", 10, "pet_egg", "rare", 0, 0},
                                {"sell_1_5", "1.5x Sell / 30m", 1, "multiplier", "", 1.5, 1800},
                                {"sell_3", "3x Sell / 30m", 3, "multiplier", "", 3, 1800},
                                {"sell_5", "5x Sell / 15m", 5, "multiplier", "", 5, 900},
                                {"automine_time", "Automine Time / 1h", 2, "automine_time", "", 0, 3600},
                                {"automine_6h", "Automine Time / 6h", 10, "automine_time", "", 0, 21600},
                                {"xp_250k", "250K Liquid EXP", 5, "liquid_xp", "", 250000, 0},
                                {"xp_1m", "1M Liquid EXP", 15, "liquid_xp", "", 1000000, 0},
                                {"rename_token", "Rename Token", 15, "rename_token", "", 0, 0},
                                {"pouch", "Small Pouch / 18 slots", 35, "pouch", "small", 0, 18},
                                {"medium_pouch", "Medium Pouch / 27 slots", 60, "pouch", "medium", 0, 27},
                                {"large_pouch", "Large Pouch / 54 slots", 125, "pouch", "large", 0, 54}};
        Json result(::Json::arrayValue);
        for (const auto& product : rows) {
            Json row, item;
            row["key"] = product.key;
            row["name"] = product.name;
            row["price"] = product.price;
            item["id"] = product.item;
            item["count"] = 1;
            const std::string family = product.item;
            if (family == "pet_egg")
                item["tier_id"] = product.tier;
            if (family == "multiplier") {
                item["boost_type"] = "sell";
                item["amount"] = product.amount;
                item["duration"] = product.duration;
            }
            if (family == "liquid_xp")
                item["amount"] = product.amount;
            if (family == "automine_time")
                item["duration"] = product.duration;
            if (family == "pouch") {
                item["tier"] = product.tier;
                item["slots"] = product.duration;
                item["items"] = Json(::Json::arrayValue);
            }
            row["item"] = item;
            result.append(row);
        }
        return result;
    }();
    return products;
}

Result Game::useItem(int index) {
    if (index < 0 || index >= int(state["inventory_slots"].size()))
        return fail("Invalid inventory slot.");
    if (index == 0)
        return {true, "", "pick"};
    const Json& item = state["inventory_slots"][index];
    const auto name = id(item);
    Json args;
    args["index"] = index;
    if (name == "crystal" || name == "pick_part") {
        if (!item.get("identified", false).asBool())
            return itemAction("identify", args);
        if (name == "pick_part") {
            int slot = 0;
            for (int i = 0; i < 3; i++)
                if (item.get("slot", "").asString() == partNames[i])
                    slot = i;
            return transfer("inventory", index, "parts", slot);
        }
        return {true, "Convert this crystal into dust?", "confirm:crystal_dust:" + std::to_string(index)};
    }
    if (name == "crystal_dust")
        return {true, "Use four dust to craft a crystal?", "confirm:crystal_craft:" + std::to_string(index)};
    if (name == "pet_egg")
        return itemAction("hatch", args);
    if (name == "pet") {
        if (state["pet_data"]["active_pet"].isObject())
            return fail("Unequip your active pet first.");
        return transfer("inventory", index, "pet", 0);
    }
    if (name == "pet_food")
        return itemAction("feed", args);
    if (name == "robot")
        return transfer("inventory", index, "robots");
    if (name == "robot_fuel")
        return {true, "", "robots"};
    if (name == "pouch")
        return {true, "", "pouch:" + std::to_string(index)};
    if (name == "enchant_book" || name == "liquid_xp")
        return success("Drag this item onto your pickaxe.");
    if (name == "automine_time" || name == "multiplier") {
        Json one = item;
        one["count"] = 1;
        if (!apply(*this, one))
            return fail("This item cannot be used.");
        consume(state["inventory_slots"], index);
        return success("Item activated.");
    }
    if (name == "rename_token")
        return success("Hold a pickaxe or pouch, then use /rename.");
    if (name == "minebomb")
        return success("Hold this bomb and left-click to throw it.");
    return fail("That slot is empty.");
}

Result Game::activateHeld(int index, bool primary) {
    if (index < 0 || index >= static_cast<int>(state["inventory_slots"].size()))
        return fail("Invalid inventory slot.");
    if (index == 0)
        return primary ? success() : Result{true, "", "pick"};
    const Json& item = state["inventory_slots"][index];
    const std::string name = id(item);
    if (name.empty())
        return success();
    Json fields;
    fields["index"] = index;
    if (name == "crystal") {
        if (primary) {
            if (item.get("identified", false).asBool())
                return success("Right-click to convert this crystal into dust.");
            return itemAction("identify", fields);
        }
        return {true, "Convert this crystal into dust?", "confirm:crystal_dust:" + std::to_string(index)};
    }
    if (name == "pick_part") {
        if (!item.get("identified", false).asBool())
            return itemAction("identify", fields);
        return primary ? success("Use Parts in the tool menu to equip this part.") : useItem(index);
    }
    if (name == "crystal_dust") {
        if (!primary)
            return success("Left-click to craft crystals from dust.");
        return {true, "Use four dust to craft a crystal?", "confirm:crystal_craft:" + std::to_string(index)};
    }
    if (name == "pet") {
        if (primary)
            return success("Use /pets to equip. Right-click to convert into pet food.");
        return {true, "Convert this pet into pet food?", "confirm:pet_food:" + std::to_string(index)};
    }
    if (name == "pet_food")
        return success("Use /pets to feed your active pet.");
    if (name == "pouch" && primary)
        return success("Right-click to open this pouch.");
    // Eggs, time, multipliers and instructions share their inventory behavior.
    // Bombs are thrown by the world controller after its input and refill checks.
    return useItem(index);
}

Json Game::multiplierRecipes() const {
    Json recipes(::Json::arrayValue);
    const auto& slots = state["inventory_slots"];
    for (unsigned i = 1; i < slots.size(); ++i) {
        const Json& item = slots[i];
        if (id(item) != "multiplier" || rewardKind(item.get("boost_type", "sell").asString()) != "money")
            continue;
        const double amount = item.get("amount", 1).asDouble(), duration = item.get("duration", 0).asDouble();
        if (!std::isfinite(amount) || !std::isfinite(duration) || amount < 1 || duration <= 0)
            continue;
        unsigned group = 0;
        while (group < recipes.size() && (recipes[group]["amount"].asDouble() != amount ||
                                          recipes[group]["duration"].asDouble() != duration))
            ++group;
        Json& recipe = recipes[group];
        if (recipe.isNull()) {
            recipe["index"] = i;
            recipe["item"] = item;
            recipe["amount"] = amount;
            recipe["duration"] = duration;
            recipe["count"] = 0;
        }
        recipe["count"] = recipe["count"].asInt() + count(item);
        Json slot;
        slot["index"] = i;
        slot["item"] = item;
        recipe["slots"].append(slot);
    }
    return recipes;
}

int Game::petFoodValue(const Json& pet) const {
    if (id(pet) != "pet")
        return 0;
    return pet.get("food_value", petTier(*this, pet.get("tier_id", "common").asString()).get("food_value", 1))
        .asInt();
}

Json Game::crystalConversionQuote(int index) const {
    const auto& slots = state["inventory_slots"];
    if (index <= 0 || index >= int(slots.size()))
        return Json();
    const auto& item = slots[index];
    const auto name = id(item);
    if (name != "crystal" && name != "crystal_dust")
        return Json();
    Json quote;
    quote["index"] = index;
    quote["item"] = item;
    quote["slots"] = Json(::Json::arrayValue);
    ::Json::Int64 total = 0;
    for (int i = 1; i < int(slots.size()); ++i) {
        if (id(slots[i]) != name || slots[i].get("tier", 1).asInt() != item.get("tier", 1).asInt() ||
            (name == "crystal" && slots[i].get("identified", false).asBool()))
            continue;
        Json row;
        row["index"] = i;
        row["item"] = slots[i];
        quote["slots"].append(row);
        total += count(slots[i]);
    }
    quote["count"] = total;
    return quote;
}

Result Game::itemAction(const std::string& action, const Json& args) {
    if (action == "robot_refuel" || action == "robot_upgrade") {
        const int index = args.get("index", -1).asInt();
        if (index < 0 || index >= 3 || id(state["robot_slots"][index]) != "robot")
            return fail("Equip a robot first.");
        Json& robot = state["robot_slots"][index];
        if (action == "robot_upgrade") {
            const Json quote = robotUpgradeQuote(index, args.get("levels", 1).asInt());
            if (quote.isMember("error"))
                return fail(quote["error"].asString());
            if (quote != args)
                return fail("The robot changed. Review the upgrade again.");
            for (const char* wallet : {"rubies", "sapphires"})
                if (state[wallet].asDouble() < quote[wallet].asDouble())
                    return fail("Not enough " + currencyName(wallet) + ".");
            for (const char* wallet : {"rubies", "sapphires"})
                state[wallet] = state[wallet].asDouble() - quote[wallet].asDouble();
            robot["level"] = robot["level"].asInt() + quote["levels"].asInt();
            return {true, "Robot upgraded to level " + std::to_string(robot["level"].asInt()) + ".",
                    "robots"};
        }
        const auto& cfg = data.systems["robots"];
        const int perItem = std::max(1, cfg.get("fuel_per_item", 250).asInt());
        const int room =
            (cfg.get("fuel_capacity", 1000000000).asInt() - robot.get("fuel", 0).asInt()) / perItem;
        if (room <= 0)
            return fail("Robot fuel is full.");
        auto& slots = state["inventory_slots"];
        int loaded = 0;
        const int selected = args.get("fuel_slot", -1).asInt();
        for (int i = 1; i < int(slots.size()) && loaded < std::min(room, 64); ++i) {
            if ((selected >= 0 && i != selected) || id(slots[i]) != "robot_fuel")
                continue;
            const int take = std::min(count(slots[i]), std::min(room, 64) - loaded);
            consume(slots, i, take);
            loaded += take;
        }
        if (!loaded)
            return fail("No Robot Fuel in your inventory. Upgrade FuelFinder to find it.");
        robot["fuel"] = robot.get("fuel", 0).asInt() + loaded * perItem;
        return success("Added " + std::to_string(loaded * perItem) + " fuel.");
    }
    if (action.rfind("equipment_set_", 0) == 0)
        return equipmentSetAction(action, args);
    if (action == "mine_select" || action == "mine_upgrade" || action.rfind("contract_", 0) == 0 ||
        action.rfind("expedition_", 0) == 0)
        return progressionAction(action, args);
    if (action == "vault_buy") {
        const int page = vaultPages();
        const double cost = nextVaultCost();
        if (page >= MaxVaultPages)
            return fail("All vault pages are unlocked.");
        if (!args["page"].isInt() || args["page"].asInt() != page || !args["cost"].isNumeric() ||
            args["cost"].asDouble() != cost)
            return fail("The vault price changed. Open the purchase again.");
        const double money = state.get("money", 0).asDouble();
        if (!std::isfinite(money) || !std::isfinite(cost) || cost <= 0 || money < cost)
            return fail("Not enough money. Need " + compact(cost) + ".");
        state["money"] = std::floor(money - cost);
        state["vault_slots"].resize((page + 1) * VaultPageSize);
        state["vault_names"].append("Vault " + std::to_string(page + 1));
        return {true, "Vault page " + std::to_string(page + 1) + " unlocked.",
                "vault:" + std::to_string(page)};
    }
    if (action == "vault_rename") {
        const int page = args["page"].isInt() ? args["page"].asInt() : -1;
        if (page < 0 || page >= vaultPages() || !args["name"].isString())
            return fail("Choose an unlocked vault page.");
        std::string name = args["name"].asString();
        const auto first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
        name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
        if (name.empty() || name.size() > 32 ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            return fail("Use a short page name without line breaks.");
        state["vault_names"][page] = name;
        return {true, "Vault page renamed.", "vault:" + std::to_string(page)};
    }
    const int index = args.isObject() ? args.get("index", -1).asInt() : -1;
    const int target = args.isObject() ? args.get("target", -1).asInt() : -1;
    Json& slots = state["inventory_slots"];
    const Json item = index > 0 && index < int(slots.size()) ? slots[index] : Json();
    const auto name = id(item);
    if (action == "identify") {
        if ((name != "crystal" && name != "pick_part") || item.get("identified", false).asBool())
            return fail("Choose an unidentified crystal or part.");
        Json planned = slots, rolled = item;
        rolled["count"] = 1;
        rolled["identified"] = true;
        const char* kinds[] = {"token", "exp", "sell"};
        rolled["boost_type"] = kinds[random() % 3];
        rolled["boost"] = rollBoost(*this, item.get("tier", 1).asInt(), name);
        rolled["uid"] = uid(*this, name);
        if (name == "pick_part" && !rolled.isMember("slot"))
            rolled["slot"] = partNames[random() % 3];
        consume(planned, index);
        if (planned[index].isNull())
            planned[index] = rolled;
        else if (!insert(planned, rolled, 1))
            return fail("Free a slot before identifying this stack.");
        slots = planned;
        return success("Item identified.");
    }
    if (action == "hatch") {
        if (name != "pet_egg")
            return fail("Choose an egg.");
        Json& eggs = state["pet_data"]["hatchery"];
        if (eggs.size() >= data.systems["pets"].get("max_hatchery_slots", 6).asUInt())
            return fail("Hatchery is full.");
        if (target >= 0 && target != int(eggs.size()))
            return fail("Use the next empty hatchery slot.");
        const Json tier = petTier(*this, item.get("tier_id", "common").asString());
        if (!tier.get("hatchable", true).asBool())
            return fail("This pet is available by merging only.");
        Json egg = rollPet(*this, tier["id"].asString());
        egg["progress"] = 0;
        egg["raw_progress"] = 0.0;
        egg["required"] = tier.get("hatch_blocks", 25000);
        egg["ready"] = false;
        eggs.append(egg);
        consume(slots, index);
        return success("Egg added to the hatchery.");
    }
    if (action == "claim") {
        Json& eggs = state["pet_data"]["hatchery"];
        if (index < 0 || index >= int(eggs.size()))
            return fail("That hatchery slot is empty.");
        Json payload = eggs[index];
        if (payload.get("ready", false).asBool()) {
            if (!payload.isMember("boost_type"))
                payload = rollPet(*this, payload.get("tier_id", "common").asString());
            for (const char* key : {"progress", "raw_progress", "required", "ready"})
                payload.removeMember(key);
            payload["id"] = "pet";
            payload["count"] = 1;
        } else {
            const std::string rarity = payload.get("tier_id", "common").asString();
            payload = Json(::Json::objectValue);
            payload["id"] = "pet_egg";
            payload["tier_id"] = rarity;
            payload["count"] = 1;
        }
        Json planned = slots;
        if (target == -1) {
            if (!insert(planned, payload, 1))
                return fail("Inventory is full.");
        } else {
            if (target <= 0 || target >= int(planned.size()) || !planned[target].isNull())
                return fail("Choose an empty inventory slot.");
            planned[target] = payload;
        }
        slots = planned;
        Json unused;
        eggs.removeIndex(index, &unused);
        return success();
    }
    if (action == "feed") {
        if (name != "pet_food")
            return fail("Choose pet food.");
        Json& pet = state["pet_data"]["active_pet"];
        if (!pet.isObject())
            return fail("Equip a pet first.");
        const Json tier = petTier(*this, pet.get("tier_id", "common").asString());
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        const double ready =
            pet.get("last_fed_ts", 0).asDouble() + tier.get("feed_cooldown", 1800).asDouble();
        if (ready > now)
            return fail("Your pet can eat again in " +
                        std::to_string(static_cast<int>(std::ceil(ready - now))) + " seconds.");
        const int multiplier =
            endgameMultiplier("hyper_surge") > 1
                ? data.enchants["effects"]["hyper_surge"].get("pet_xp_multiplier", 2).asInt()
                : 1;
        addPetExperience((10 + int(random() % 41)) * multiplier);
        pet["last_fed_ts"] = ::Json::Int64(now);
        consume(slots, index);
        return success("Pet fed.");
    }
    if (action == "merge_pets") {
        Json planned = slots;
        int found = 0;
        std::map<std::string, int> types = {{"sell", 0}, {"token", 0}, {"exp", 0}};
        std::map<std::string, int> mutations;
        for (unsigned i = 1; i < planned.size() && found < 5; ++i)
            if (id(planned[i]) == "pet" && planned[i].get("tier_id", "").asString() == "mythical") {
                std::string kind = planned[i].get("boost_type", "sell").asString();
                if (kind == "money")
                    kind = "sell";
                if (kind == "xp")
                    kind = "exp";
                ++types[kind];
                const auto& trait = petMutation(planned[i]);
                if (trait.isObject())
                    ++mutations[trait["id"].asString()];
                consume(planned, int(i));
                ++found;
            }
        if (found < 5)
            return fail("Put five Mythical pets in your inventory first.");
        std::string kind = "sell";
        for (const auto* candidate : {"token", "exp"})
            if (types[candidate] > types[kind])
                kind = candidate;
        Json pet = rollPet(*this, "overpowered");
        pet["boost_type"] = kind;
        int inherited = 0;
        for (const auto& trait : mutations)
            if (trait.second > inherited) {
                pet["mutation"] = trait.first;
                inherited = trait.second;
            }
        if (inherited) {
            pet["mutation_rolled"] = true;
            pet["pet_color"] = petMutation(pet)["color"];
        } else
            rollPetMutation(pet);
        if (!insert(planned, pet, 1))
            return fail("Inventory is full.");
        slots = planned;
        return success("Merged five Mythicals into an OVERPOWERED pet.");
    }
    if (action == "crystal_dust" || action == "dust" || action == "crystal_craft" || action == "craft" ||
        action == "pet_food" || action == "food" || action == "convert") {
        std::string operation = action;
        if (action == "convert")
            operation = args.get("operation", "").asString();
        if (operation == "dust")
            operation = "crystal_dust";
        if (operation == "craft")
            operation = "crystal_craft";
        if (operation == "food")
            operation = "pet_food";
        if (operation == "pet_food") {
            if (args.get("all", false).asBool())
                return fail("Select the pets to convert first.");
            Json selected = args["pets"];
            if (!args.isMember("pets")) {
                Json row;
                row["index"] = index;
                row["item"] = args.get("item", item);
                selected = Json(::Json::arrayValue);
                selected.append(row);
            }
            if (!selected.isArray() || selected.empty() || selected.size() >= slots.size())
                return fail("Select the pets to convert first.");
            Json planned = slots;
            std::set<int> seen;
            std::int64_t food = 0;
            for (const auto& row : selected) {
                if (!row.isObject() || !row["index"].isInt())
                    return fail("Choose a pet from your inventory.");
                const int slot = row["index"].asInt();
                if (slot <= 0 || slot >= int(slots.size()) || !seen.insert(slot).second ||
                    id(slots[slot]) != "pet")
                    return fail("Choose a pet from your inventory.");
                if (row["item"] != slots[slot])
                    return fail("A selected pet changed. Select it again.");
                const int value = petFoodValue(slots[slot]);
                if (value <= 0)
                    return fail("This pet cannot be converted into food.");
                food += value;
                consume(planned, slot);
            }
            if (food > 100000)
                return fail("Not enough inventory space for the food.");
            Json output;
            output["id"] = "pet_food";
            output["count"] = int(food);
            if (!insert(planned, output, 1))
                return fail("Not enough inventory space for the food.");
            slots = std::move(planned);
            return success("Converted " + std::to_string(selected.size()) +
                           (selected.size() == 1 ? " pet into " : " pets into ") +
                           std::to_string(food) + " pet food.");
        }
        if (operation != "crystal_dust" && operation != "crystal_craft")
            return fail("Unknown conversion.");
        const std::string expected = operation == "crystal_dust" ? "crystal" : "crystal_dust";
        if (name != expected)
            return fail("Choose an item to convert.");
        if (args.isMember("item") && args["item"] != item)
            return fail("Item changed. Review the conversion again.");
        const bool all = args.get("all", false).asBool();
        if (all && item.get("identified", false).asBool())
            return fail("Bulk conversion is unavailable for this item.");
        if (all && args.isMember("slots") && args["slots"] != crystalConversionQuote(index)["slots"])
            return fail("Matching items changed. Review the conversion again.");
        Json planned = slots;
        bool space = false;
        for (unsigned i = 1; i < planned.size(); ++i)
            if (planned[i].isNull())
                space = true;
        if (!space)
            return fail("Free an inventory slot first.");
        const int tier = item.get("tier", 1).asInt();
        Json outputs(::Json::arrayValue);
        if (operation == "crystal_dust" && !all) {
            consume(planned, index);
            Json output;
            output["id"] = "crystal_dust";
            output["tier"] = tier;
            output["count"] = 1;
            outputs.append(output);
        } else {
            ::Json::Int64 total = 0;
            for (unsigned i = 1; i < planned.size(); ++i)
                if (id(planned[i]) == expected && planned[i].get("tier", 1).asInt() == tier &&
                    (operation != "crystal_dust" || !planned[i].get("identified", false).asBool()))
                    total += count(planned[i]);
            if (operation == "crystal_craft" && total < 4)
                return fail("You need four matching dust.");
            const auto amount = operation == "crystal_dust" ? total : (all ? total / 4 * 4 : 4);
            if (amount / 4 > 100000)
                return fail("Not enough inventory space for the result.");
            auto needed = amount;
            for (unsigned i = 1; i < planned.size() && needed > 0; ++i)
                if (id(planned[i]) == expected && planned[i].get("tier", 1).asInt() == tier &&
                    (operation != "crystal_dust" || !planned[i].get("identified", false).asBool())) {
                    int take = int(std::min<::Json::Int64>(needed, count(planned[i])));
                    consume(planned, int(i), take);
                    needed -= take;
                }
            if (amount / 4 > 0) {
                Json output;
                output["id"] = "crystal";
                output["tier"] = tier + 1;
                output["identified"] = false;
                output["count"] = amount / 4;
                outputs.append(output);
            }
            if (operation == "crystal_dust" && amount % 4 > 0) {
                Json output;
                output["id"] = "crystal_dust";
                output["tier"] = tier;
                output["count"] = amount % 4;
                outputs.append(output);
            }
        }
        for (const auto& output : outputs)
            if (!insert(planned, output, 1))
                return fail("Not enough inventory space for the result.");
        slots = planned;
        return success("Items converted.");
    }
    if (action == "book" || action == "xp") {
        if (name != (action == "book" ? "enchant_book" : "liquid_xp"))
            return fail("Use an enchant book or Liquid EXP on your pickaxe.");
        if (!apply(*this, item))
            return fail("This item cannot be applied.");
        consume(slots, index, count(item));
        return success("Applied to your pickaxe.");
    }
    if (action == "salvage_parts") {
        const auto& selected = args["parts"];
        if (!selected.isArray() || selected.empty() || selected.size() >= slots.size())
            return fail("Select the parts to salvage first.");
        bool allowed = false;
        for (const auto& eligible : data.systems["salvage"]["eligible_items"])
            if (eligible == "pick_part")
                allowed = true;
        if (!allowed)
            return fail("Parts cannot be salvaged.");
        std::set<int> seen;
        std::int64_t total = 0;
        for (const auto& row : selected) {
            if (!row.isObject() || !row["index"].isInt())
                return fail("Choose a part from your inventory.");
            const int slot = row["index"].asInt();
            if (slot <= 0 || slot >= int(slots.size()) || !seen.insert(slot).second ||
                id(slots[slot]) != "pick_part")
                return fail("Choose a part from your inventory.");
            if (row["item"] != slots[slot])
                return fail("A selected part changed. Select it again.");
            total += count(slots[slot]);
        }
        for (const auto& row : selected) {
            const int slot = row["index"].asInt();
            salvage(*this, slots[slot]);
            consume(slots, slot, count(slots[slot]));
        }
        return success("Salvaged " + std::to_string(total) + (total == 1 ? " part." : " parts."));
    }
    if (action == "salvage") {
        if (args.isMember("item") && args["item"] != item)
            return fail("Item changed. Select it again.");
        if (!item.isObject() || !salvage(*this, item))
            return fail("This item cannot be salvaged.");
        consume(slots, index, count(item));
        return success("Item salvaged.");
    }
    if (action == "buy_point") {
        for (const auto& product : pointShopProducts())
            if (args.get("key", "") == product["key"]) {
                const int price = product["price"].asInt();
                if (state.get("points", 0).asDouble() < price)
                    return fail("Not enough points.");
                if (!giveItem(product["item"]))
                    return fail("Inventory is full.");
                state["points"] = state.get("points", 0).asDouble() - price;
                return success("Item purchased.");
            }
        return fail("Unknown shop item.");
    }
    if (action == "endmulti") {
        Json& rows = state["boosts"]["money"];
        int selected = -1;
        double shortest = 1e100;
        for (unsigned i = 0; i < rows.size(); ++i) {
            if (!rows[i].isObject())
                continue;
            double remaining = rows[i].get("remaining", 0).asDouble();
            if (remaining > 0 && remaining < shortest) {
                shortest = remaining;
                selected = int(i);
            }
        }
        if (selected < 0)
            return fail("No active sell multiplier.");
        Json output;
        output["id"] = "multiplier";
        output["boost_type"] = "sell";
        output["count"] = 1;
        output["amount"] = rows[selected]["amount"];
        output["duration"] = shortest;
        if (!giveItem(output, false))
            return fail("Make room in your inventory first.");
        Json unused;
        rows.removeIndex(selected, &unused);
        return success("Sell multiplier returned to your inventory.");
    }
    if (action == "combinemulti") {
        if (name != "multiplier" || rewardKind(item.get("boost_type", "sell").asString()) != "money")
            return fail("Choose a sell multiplier.");
        if (args.isMember("item") && args["item"] != item)
            return fail("Items changed. Choose the multipliers again.");
        double amount = item.get("amount", 1).asDouble(), duration = item.get("duration", 0).asDouble();
        if (!std::isfinite(amount) || !std::isfinite(duration) || amount < 1 || duration <= 0 ||
            duration > std::numeric_limits<double>::max() / 3)
            return fail("Invalid multiplier.");
        if (args.isMember("slots")) {
            bool unchanged = false;
            for (const auto& recipe : multiplierRecipes())
                if (recipe["amount"].asDouble() == amount && recipe["duration"].asDouble() == duration &&
                    recipe["slots"] == args["slots"])
                    unchanged = true;
            if (!unchanged)
                return fail("Items changed. Choose the multipliers again.");
        }
        if (amount >= 25)
            return fail("That multiplier is already 25x.");
        Json planned = slots;
        int needed = 4;
        for (unsigned i = 1; i < planned.size() && needed > 0; ++i)
            if (id(planned[i]) == "multiplier" &&
                rewardKind(planned[i].get("boost_type", "sell").asString()) == "money" &&
                planned[i].get("amount", 1).asDouble() == amount &&
                planned[i].get("duration", 0).asDouble() == duration) {
                int take = std::min(needed, count(planned[i]));
                consume(planned, int(i), take);
                needed -= take;
            }
        if (needed > 0)
            return fail("You need four matching sell multipliers.");
        Json output;
        output["id"] = "multiplier";
        output["boost_type"] = "sell";
        output["count"] = 1;
        output["amount"] = std::min(25.0, amount * 2);
        output["duration"] = duration * 3;
        if (!insert(planned, output, 1))
            return fail("Make room for the combined multiplier.");
        slots = planned;
        return success("Combined four sell multipliers.");
    }
    if (action == "autominer_start") {
        if (inShaft())
            return fail("Automining is unavailable in challenge shafts.");
        if (state.get("autominer_time", 0).asDouble() <= 0)
            return fail("You need Automine Time.");
        if (state["autominer_time"].asDouble() < autominerTickCost()) {
            state["autominer_time"] = 0.0;
            state["autominer_active"] = false;
            return fail("Not enough automine time for this speed.");
        }
        const int selected = std::clamp(state.get("selected_hotbar_index", 0).asInt(), 0, 8);
        if (id(state["inventory_slots"][selected]) != "pickaxe")
            return fail("Hold your prison pickaxe to start Autominer.");
        autominerElapsed_ = 0;
        state["_autominer_duration"] = state["autominer_time"];
        state["autominer_active"] = true;
        return {true, "Autominer started.", "autominer:start"};
    }
    if (action == "autominer_stop") {
        autominerElapsed_ = 0;
        state["autominer_active"] = false;
        return {true, "Autominer stopped.", "autominer:stop"};
    }
    if (action == "autominer_upgrade") {
        const Json& cfg = data.systems["autominer"];
        int bps = state.get("autominer_unlocked_bps", cfg.get("default_bps", 3)).asInt();
        if (bps >= cfg.get("max_bps", 10).asInt())
            return fail("Autominer is maxed.");
        double cost = cfg.get("upgrade_base_cost", 1000000000).asDouble() +
                      (bps - 1) * cfg.get("upgrade_step", 250).asDouble();
        double money = state.get("money", 0).asDouble();
        if (!std::isfinite(money) || money < cost)
            return fail("Not enough money for this upgrade.");
        state["money"] = std::floor(money - cost);
        state["autominer_unlocked_bps"] = bps + 1;
        state["autominer_bps"] = bps + 1;
        return success("Autominer upgraded.");
    }
    if (action == "autominer_bps") {
        int bps = args.get("value", 0).asInt();
        if (bps < 1 || bps > state.get("autominer_unlocked_bps", 3).asInt())
            return fail("Buy more autominer speed first.");
        state["autominer_bps"] = bps;
        return success("Autominer speed updated.");
    }
    if (action == "filter" || action == "filter_rule")
        return configureFilter(action, args);
    if (action == "rename_begin" || action == "rename" || action == "rename_preview") {
        const int selected = state.get("selected_hotbar_index", 0).asInt();
        if (selected < 0 || selected >= int(slots.size()))
            return fail("Hold a pickaxe or pouch.");
        Json& held = slots[selected];
        if (id(held) != "pickaxe" && id(held) != "pouch")
            return fail("Hold a pickaxe or pouch.");
        int token = -1;
        for (unsigned i = 1; i < slots.size(); ++i)
            if (id(slots[i]) == "rename_token") {
                token = int(i);
                break;
            }
        if (token < 0)
            return fail("You need a Rename Token. Buy one in /pointshop.");
        if (action == "rename_begin") {
            state["_rename_target"] = id(held) == "pickaxe" ? "pickaxe" : held.get("uid", "").asString();
            state["_rename_old"] =
                selected == 0 ? state.get("pick_custom_name", "Prison Tool") : held.get("custom_name", "");
            return {true, "Colors: &#rrggbb or &0–&f. Formatting: &l, &o, &n, &m, &r. Maximum 40 characters.",
                    "rename"};
        }
        const auto targetId = id(held) == "pickaxe" ? "pickaxe" : held.get("uid", "").asString();
        if (state.get("_rename_target", "").asString() != targetId ||
            state.get("_rename_old", Json()) !=
                (selected == 0 ? state.get("pick_custom_name", "Prison Tool") : held.get("custom_name", "")))
            return fail("Held item changed. Use /rename again.");
        std::string text = args.get("text", "").asString();
        if (text.size() > 1024)
            return fail("Name is too long.");
        for (char& c : text) {
            if (c == '\r' || c == '\n' || c == '\t')
                c = ' ';
            else if (static_cast<unsigned char>(c) < 32 || c == 127)
                return fail("Name must be one line.");
        }
        const auto first = text.find_first_not_of(' '), last = text.find_last_not_of(' ');
        if (first == std::string::npos)
            return fail("Name cannot be empty.");
        text = text.substr(first, last - first + 1);
        const auto plain = plainName(text);
        const auto characters =
            std::count_if(plain.begin(), plain.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
        if (plain.find_first_not_of(' ') == std::string::npos || characters > 40)
            return fail("Name must contain 1–40 visible characters.");
        if (action == "rename_preview") {
            state["_rename_pending"] = text;
            return {true, "", "rename:preview"};
        }
        if (state.get("_rename_pending", "").asString() != text)
            return fail("Preview the name before confirming.");
        if (selected == 0)
            state["pick_custom_name"] = text;
        else
            held["custom_name"] = text;
        consume(slots, token);
        state.removeMember("_rename_target");
        state.removeMember("_rename_old");
        state.removeMember("_rename_pending");
        return success("Item renamed.");
    }
    if (action == "rewards")
        return {true, "Rewards are granted automatically at each mining milestone.", "rewards"};
    if (action == "roll_pet") {
        if (!state.get("devmode", false).asBool())
            return fail("This requires Devmode.");
        Json pet = rollPet(*this, args.get("rarity", "common").asString());
        if (args.isMember("pet_type"))
            pet["pet_type"] = args["pet_type"];
        if (!giveItem(pet))
            return fail("Inventory is full.");
        return success("Pet added.");
    }
    return fail("Unknown item action: " + action);
}

void Game::tickItems(double, int blocks, int hatcheryLevel) {
    if (blocks <= 0)
        return;
    const double scale =
        1 +
        std::clamp(hatcheryLevel >= 0 ? hatcheryLevel : state["enchants"].get("hatchery", 0).asInt(), 0, 5) *
            .03;
    for (auto& egg : state["pet_data"]["hatchery"])
        if (egg.isObject() && !egg.get("ready", false).asBool()) {
            double raw =
                egg.get("raw_progress", egg.get("progress", 0).asDouble() / scale).asDouble() + blocks;
            const double required = std::max(1.0, egg.get("required", 1).asDouble());
            egg["raw_progress"] = raw;
            egg["progress"] = std::min(required, std::floor(raw * scale));
            egg["ready"] = egg["progress"].asDouble() >= required;
            if (egg["ready"].asBool()) {
                rollPetMutation(egg);
                recordProgress("hatches", 1, captureRewardContext());
            }
        }
}

std::string Game::itemName(const Json& item) const {
    const auto name = id(item);
    if (name.empty())
        return "";
    if (name == "pickaxe") {
        int tier = state.get("parts_prestige", 0).asInt() + 1;
        return plainName(state.get("pick_custom_name", "Prison Tool").asString()) +
               (tier > 1 ? " (P" + std::to_string(tier) + ")" : "");
    }
    if (name == "pouch" && item.isMember("custom_name") && !item["custom_name"].asString().empty())
        return plainName(item["custom_name"].asString());
    if (name == "pet_egg")
        return petTier(*this, item.get("tier_id", "common").asString()).get("label", "Common").asString() +
               " Egg";
    if (name == "pet")
        return item.get("mood", "Joyful").asString() + " " + item.get("pet_type", "Rabbit").asString() +
               " Pet";
    if (name == "crystal" || name == "crystal_dust" || name == "pick_part") {
        std::string caption = name == "crystal_dust" ? "Dust"
                              : name == "crystal"    ? "Crystal"
                                                     : title(item.get("slot", "head").asString()) + " Part";
        if (name != "crystal_dust" && !item.get("identified", false).asBool())
            caption = name == "pick_part" ? "Unidentified Part" : "Unidentified Crystal";
        return "T" + std::to_string(std::max(1, item.get("tier", 1).asInt())) + " " + caption;
    }
    if (name == "pouch")
        return title(item.get("tier", "small").asString()) + " Pouch";
    if (name == "minebomb")
        return "Mine Bomb";
    if (name == "multiplier")
        return displayKind(item.get("boost_type", "sell").asString()) + " Multiplier";
    if (name == "enchant_book")
        return "Enchanted Book";
    if (name == "liquid_xp")
        return "Liquid EXP";
    if (name == "automine_time")
        return "Automine Time";
    if (name == "rename_token")
        return "Rename Token";
    if (name == "pet_food")
        return "Pet Food";
    if (name == "robot")
        return displayKind(item.get("boost_type", "money").asString()) + "Boost Robot";
    if (name == "robot_fuel")
        return "Robot Fuel";
    return item.get("description", title(name)).asString();
}
std::vector<std::string> Game::itemLore(const Json& item) const {
    auto durationText = [](double value) {
        auto seconds = static_cast<std::int64_t>(std::clamp(value, 0.0, 9e15));
        return std::to_string(seconds / 3600) + "h " + std::to_string(seconds / 60 % 60) + "m " +
               std::to_string(seconds % 60) + "s";
    };
    auto boostLabel = [](const Json& value) {
        return displayKind(value.get("boost_type", "sell").asString()) + " Boost";
    };
    std::vector<std::string> lines{itemName(item)};
    const auto name = id(item);
    if (name == "pickaxe") {
        const int level = state.get("pick_level", 1).asInt();
        const double xp = state.get("pick_xp", 0).asDouble(), needed = pickXpNeeded(level);
        lines.insert(lines.end(), {"", "Level: " + std::to_string(level) + " [||||||||||||]",
                                   "EXP: " + compact(xp) + "/" + compact(needed), ""});
        const bool showMax =
            state["message_settings"]
                .get("show_max_enchant_label", state["settings"].get("show_max_labels", true))
                .asBool();
        for (const auto& key : data.enchants["order"]) {
            const std::string enchant = key.asString();
            const int current = state["enchants"].get(enchant, 0).asInt();
            if (current <= 0)
                continue;
            const auto& def = data.enchants["defs"][enchant];
            const int maximum = def.get("max", -1).asInt();
            lines.push_back(def.get("label", enchant).asString() + ": " +
                            (showMax && maximum > 0 && current == maximum ? "MAX" : std::to_string(current)));
        }
        lines.push_back("");
        if (crystalSlots() == 0) {
            int firstLevel =
                data.crystals["slot_levels"].empty() ? 20 : data.crystals["slot_levels"][0].asInt();
            lines.push_back("> Crystal slots unlock at Level " + std::to_string(firstLevel));
        } else {
            for (int i = 0; i < crystalSlots(); ++i) {
                const auto& crystal = state["equipped_crystals"][i];
                lines.push_back(crystal.isObject() ? "> " +
                                                         std::to_string(static_cast<int>(std::round(
                                                             crystal.get("boost", 0).asDouble() * 100))) +
                                                         "% " + boostLabel(crystal)
                                                   : "> Empty Crystal Slot");
            }
        }
        for (const auto& part : state["equipped_parts"])
            if (part.isObject() && part["specialization"].isString()) {
                const auto& profile = specialization(part["specialization"].asString());
                if (profile.isObject())
                    lines.push_back(profile["label"].asString() + ": " + profile["description"].asString());
            }
    } else if (name == "robot") {
        std::ostringstream bonus;
        bonus << std::fixed << std::setprecision(1) << (robotMultiplier(item) - 1) * 100;
        lines.push_back("Level: " + std::to_string(item.get("level", 1).asInt()) + "/4000");
        lines.push_back("+" + bonus.str() + "% " + boostLabel(item));
        lines.push_back("Fuel: " + std::to_string(item.get("fuel", 0).asInt()));
        lines.push_back("Uses " + std::to_string(robotFuelCost(item)) + " fuel per mined block.");
        lines.push_back("One robot per boost type. Maximum 5x each.");
        lines.push_back("Right-click to equip. Manage with /robots.");
    } else if (name == "robot_fuel") {
        lines.push_back(std::to_string(data.systems["robots"].get("fuel_per_item", 250).asInt()) +
                        " fuel per piece.");
        lines.push_back("Drag onto a robot or use Load Fuel in /robots.");
        lines.push_back("Found by FuelFinder while manually mining.");
    } else if (name == "enchant_book") {
        const std::string key = item.get("enchant_key", "").asString();
        lines.push_back(std::to_string(std::max(1, item.get("amount", 1).asInt())) + "+ " +
                        data.enchants["defs"][key].get("label", "Random Enchant").asString());
        lines.push_back(enchantCurrency(key) == "tokens"
                            ? "Drag to apply onto your pickaxe"
                            : "This enchant now requires " + currencyName(enchantCurrency(key)) + ".");
    } else if (name == "minebomb") {
        lines.push_back("Tier: " + std::to_string(std::max(1, item.get("tier", 1).asInt())));
        lines.push_back("Left click to throw");
    } else if (name == "liquid_xp") {
        lines.push_back("EXP:" + compact(item.get("amount", item.get("xp", 0)).asDouble()));
        lines.push_back("Drag onto your pickaxe!");
    } else if (name == "crystal" || name == "pick_part") {
        if (item.get("identified", false).asBool()) {
            if (item.get("guardian_forged", false).asBool())
                lines.push_back("Guardian EXP part · 10% stronger roll.");
            if (name == "pick_part" && item["specialization"].isString()) {
                const auto& profile = specialization(item["specialization"].asString());
                if (profile.isObject()) {
                    lines.push_back(profile["label"].asString());
                    lines.push_back(profile["description"].asString());
                }
            }
            lines.push_back(
                "+" + std::to_string(static_cast<int>(std::round(item.get("boost", 0).asDouble() * 100))) +
                "% " + boostLabel(item));
            lines.push_back(name == "crystal" ? "Right-click to convert into dust."
                                              : "Right-click to equip.");
            if (name == "crystal")
                lines.push_back("Drag to a crystal slot to equip.");
        } else {
            lines.push_back(name == "pick_part" ? "Held: left or right-click to identify."
                                                : "Held: left-click to identify, right-click to dust.");
            lines.push_back("Inventory: right-click to identify.");
        }
    } else if (name == "crystal_dust") {
        lines.push_back("4 dust makes one T" + std::to_string(std::max(1, item.get("tier", 1).asInt()) + 1) +
                        " crystal.");
        lines.push_back("Held: left-click to craft.");
        lines.push_back("Inventory: right-click to craft.");
    } else if (name == "pet_egg") {
        lines.push_back("Right-click to start hatching");
    } else if (name == "pet") {
        const Json tier = petTier(*this, item.get("tier_id", "common").asString());
        const auto& mutation = petMutation(item);
        if (mutation.isObject()) {
            lines.push_back("Mutation: " + mutation["label"].asString());
            lines.push_back(mutation["description"].asString());
        }
        const Json& feeding = data.systems["pets"];
        const int level = std::clamp(item.get("level", 1).asInt(), 1, feeding.get("max_level", 75).asInt());
        const int softcap = feeding.get("growth_softcap_level", 30).asInt();
        const double growth = std::min(level - 1, softcap - 1) + std::max(0, level - softcap) * .25;
        const double effect = std::min(feeding.get("active_boost_cap_pct", 650).asDouble(),
                                       item.get("base_boost_pct", 0).asDouble() *
                                           (1 + growth * feeding.get("growth_per_level", .035).asDouble()));
        const auto lastFed = static_cast<std::time_t>(item.get("last_fed_ts", 0).asDouble());
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        const double remaining =
            lastFed > 0 ? std::max(0.0, double(lastFed) + tier.get("feed_cooldown", 1800).asDouble() - now)
                        : 0;
        std::string date = "Never";
        if (lastFed > 0) {
            if (const auto* local = std::localtime(&lastFed)) {
                std::ostringstream formatted;
                formatted << std::put_time(local, "%d/%m/%Y %H:%M:%S");
                date = formatted.str();
            }
        }
        lines.insert(lines.end(), {"", "Rarity: " + tier.get("label", "Common").asString(),
                                   "Effect +" + percent(effect) + "% " + boostLabel(item), "",
                                   "Level: " + std::to_string(level),
                                   "EXP: " + std::to_string(item.get("exp", 0).asInt()) + "/" +
                                       std::to_string(feeding.get("exp_per_level", 100).asInt()),
                                   "", "Last fed: " + date,
                                   "Cooldown: " + (remaining > 0 ? durationText(remaining) : "Ready"), "",
                                   "Equip in /pets", "Inventory: right-click to equip.",
                                   "Held: right-click to convert into food."});
    } else if (name == "pouch") {
        int stored = 0;
        for (const auto& row : item["items"])
            if (row.isObject())
                ++stored;
        lines.push_back("Right Click to Open");
        lines.push_back("Stored: " + std::to_string(stored) + "/" +
                        std::to_string(item.get("slots", 18).asInt()));
    } else if (name == "automine_time") {
        lines.push_back("Duration: " + durationText(item.get("duration", 3600).asDouble()));
    } else if (name == "multiplier") {
        std::ostringstream value;
        value << std::fixed << std::setprecision(1) << item.get("amount", 1.5).asDouble();
        lines.push_back("Multiplier: " + value.str());
        lines.push_back("Duration: " + durationText(item.get("duration", 1800).asDouble()));
        lines.push_back("Right-click to activate.");
    } else if (name == "rename_token")
        lines.push_back("Hold a pickaxe or pouch, then use /rename.");
    else if (name == "pet_food")
        lines.push_back("Right-click to feed your active pet.");
    return lines;
}
} // namespace prison
