#include "prison/game.hpp"
#include <set>

namespace prison {
namespace {
constexpr int supplyLimit = 1000000000;
int amount(const Json& value, int maximum = supplyLimit) {
    if (!value.isNumeric() || !std::isfinite(value.asDouble()))
        return 0;
    return int(std::clamp(value.asDouble(), 0.0, double(maximum)));
}
const Json& find(const Json& rows, const std::string& id) {
    for (const auto& row : rows)
        if (row.get("id", "") == id)
            return row;
    static const Json empty;
    return empty;
}
} // namespace

void Game::loadProgression() {
    Json old = state["progression"].isObject() ? state["progression"] : Json(::Json::objectValue);
    for (const char* key : {"materials", "upgrades"})
        if (!old[key].isObject())
            old[key] = Json(::Json::objectValue);
    Json value(::Json::objectValue);
    const auto& regions = data.systems["regions"];
    const std::string selected = old.isObject() && old["mine"].isString() ? old["mine"].asString() : "quarry";
    const auto& region = find(regions, selected);
    value["mine"] =
        region.isObject() && region["rebirth"].asInt() <= state["rebirth"].asDouble() ? selected : "quarry";
    // retired supplies convert once; the rebuilt progression object drops the old balances.
    for (const char* key : {"stone_shard", "frost_shard", "deep_iron", "ember_core", "guardian_core"})
        state["points"] = std::min(9e15, state["points"].asDouble() + amount(old["materials"][key]));
    const double crateMoney[] = {1e18, 2e21, 5e22};
    const int cratePoints[] = {1000, 7500, 50000};
    for (int i = 0; i < amount(old["upgrades"]["crate_quality"], 3); ++i) {
        state["money"] =
            std::min(data.balance["currency_cap"].asDouble(), state["money"].asDouble() + crateMoney[i]);
        state["points"] = std::min(9e15, state["points"].asDouble() + cratePoints[i]);
    }
    for (const auto& row : data.systems["mine_upgrades"]) {
        const auto key = row["id"].asString();
        value["upgrades"][key] = amount(old["upgrades"][key], int(row["levels"].size()));
    }
    value["contracts_completed"] = amount(old["contracts_completed"]);
    value["contract_serial"] = amount(old["contract_serial"]);
    value["contract"] = Json();
    const auto& active = old["contract"];
    if (active.isObject() && active["id"].isString() && active["region"].isString()) {
        const auto& definition = find(data.systems["contracts"], active["id"].asString());
        const auto& place = find(regions, active["region"].asString());
        if (definition.isObject() && place.isObject()) {
            Json contract = definition;
            contract["goal"] = std::max(1, amount(active["goal"], 100000000));
            contract["progress"] = amount(active["progress"], contract["goal"].asInt());
            contract["points"] = amount(active["points"], 22000) + amount(active["materials"], 1000);
            contract.removeMember("materials");
            contract["region"] = place["id"];
            contract["serial"] = std::max(1, amount(active["serial"]));
            value["contract_serial"] = std::max(value["contract_serial"].asInt(), contract["serial"].asInt());
            value["contract"] = std::move(contract);
        } else if ((active["kind"] == "caves" || active["kind"] == "crates") &&
                   amount(active["goal"]) > 0 && amount(active["progress"]) >= amount(active["goal"])) {
            state["points"] = std::min(9e15, state["points"].asDouble() + amount(active["points"], 10000) +
                                               amount(active["materials"], 1000));
        }
    }
    value["expedition_serial"] = amount(old["expedition_serial"]);
    value["expeditions"] = Json(::Json::arrayValue);
    std::set<int> serials;
    if (old["expeditions"].isArray())
        for (const auto& saved : old["expeditions"]) {
            if (!saved.isObject() || !saved["pet"].isObject() || saved["pet"].get("id", "") != "pet" ||
                value["expeditions"].size() >= 3)
                continue;
            Json trip = saved;
            const auto& place = find(regions, saved["region"].isString() ? saved["region"].asString() : "");
            trip["region"] = place.isObject() ? place["id"] : Json("quarry");
            trip["points"] = amount(saved["points"], 2000) + amount(saved["materials"], 1000);
            trip.removeMember("material");
            trip.removeMember("materials");
            trip["goal"] = std::max(1, amount(saved["goal"], 100000000));
            trip["progress"] = amount(saved["progress"], trip["goal"].asInt());
            trip["pet"]["count"] = 1;
            int serial = amount(saved["serial"]);
            if (serial == 0 || serials.count(serial))
                serial = std::min(supplyLimit, value["expedition_serial"].asInt() + 1);
            while (serials.count(serial))
                --serial;
            trip["serial"] = serial;
            serials.insert(serial);
            value["expedition_serial"] = std::max(value["expedition_serial"].asInt(), serial);
            trip["loot"] = Json(::Json::arrayValue);
            if (saved["loot"].isArray())
                for (const auto& item : saved["loot"]) {
                    if (!item.isObject() ||
                        (item.get("id", "") != "pet_egg" && item.get("id", "") != "pet_food") ||
                        trip["loot"].size() >= 2)
                        continue;
                    Json drop = item;
                    drop["count"] = std::max(1, amount(item.get("count", 1), 64));
                    trip["loot"].append(std::move(drop));
                }
            value["expeditions"].append(std::move(trip));
        }
    state["progression"] = std::move(value);
}

Json Game::mineOptions() const {
    Json value;
    value["region"] = state["progression"].get("mine", "quarry");
    value["upgrades"] = state["progression"]["upgrades"];
    if (state.get("devmode", false).asBool() && state["dev_mine_prestige"].isInt())
        value["size_prestige"] = state["dev_mine_prestige"];
    if (inShaft()) {
        value["region"] = state["shaft"]["floor"]["region"];
        value["shaft"]["depth"] = state["shaft"]["floor"]["depth"];
        value["shaft"]["condition"] = state["shaft"]["floor"]["condition"];
    }
    return value;
}

Json Game::mineUpgradeQuote(const std::string& id) const {
    const auto& definition = find(data.systems["mine_upgrades"], id);
    if (!definition.isObject())
        return Json();
    const int level = amount(state["progression"]["upgrades"][id], int(definition["levels"].size()));
    Json quote;
    quote["id"] = id;
    quote["level"] = level;
    quote["max"] = int(definition["levels"].size());
    if (level < int(definition["levels"].size())) {
        quote["money"] = definition["levels"][level]["money"];
        quote["points"] = definition["levels"][level]["points"];
        quote["rebirth"] = definition["levels"][level]["rebirth"];
    }
    return quote;
}

Result Game::progressionAction(const std::string& action, const Json& args) {
    const std::string id = args["id"].isString() ? args["id"].asString() : "";
    if (action == "mine_select") {
        if (inShaft())
            return {false, "Leave the shaft before changing mines."};
        const auto& region = find(data.systems["regions"], id);
        if (!region.isObject() || region["rebirth"].asInt() > state["rebirth"].asDouble())
            return {false, "That mine is locked."};
        if (state["progression"]["mine"] == id)
            return {false, "You are already in that mine."};
        if (state.get("autominer_active", false).asBool())
            return {false, "Stop automining before changing mines."};
        state["progression"]["mine"] = id;
        return {true, "Entered " + region["label"].asString() + ".", "mines", true};
    }
    if (action == "mine_upgrade") {
        const Json quote = mineUpgradeQuote(id);
        if (quote.isNull() || quote["level"] == quote["max"])
            return {false, "That upgrade is already complete."};
        if (args != quote)
            return {false, "The upgrade changed. Open the purchase again."};
        if (quote["rebirth"].asInt() > state["rebirth"].asDouble())
            return {false, "Requires rebirth " + quote["rebirth"].asString() + "."};
        const double money = state["money"].asDouble(), cost = quote["money"].asDouble();
        const double points = state["points"].asDouble(), pointCost = quote["points"].asDouble();
        if (!std::isfinite(money) || !std::isfinite(points) || money < cost || points < pointCost)
            return {false, "Not enough money or points."};
        state["money"] = money - cost;
        state["points"] = points - pointCost;
        state["progression"]["upgrades"][id] = quote["level"].asInt() + 1;
        addStatistic("mine_upgrades_bought");
        return {true, "Mine upgraded. Applies on the next refill.", "mine_upgrades"};
    }
    if (action == "contract_accept") {
        if (state["progression"]["contract"].isObject())
            return {false, "Finish or cancel your current contract first."};
        const auto choices = contractChoices();
        const auto& quote = find(choices, id);
        if (quote.isNull() || quote != args)
            return {false, "The contract changed. Choose it again."};
        const int serial = amount(state["progression"]["contract_serial"]);
        if (serial == supplyLimit)
            return {false, "Contract limit reached."};
        Json contract = quote;
        contract["serial"] = serial + 1;
        contract["progress"] = 0;
        state["progression"]["contract_serial"] = serial + 1;
        state["progression"]["contract"] = std::move(contract);
        return {true, "Contract accepted.", "contracts"};
    }
    if (action == "contract_claim" || action == "contract_cancel") {
        const Json contract = state["progression"]["contract"];
        if (!contract.isObject() || args["serial"] != contract["serial"])
            return {false, "That contract is no longer active."};
        if (action == "contract_cancel") {
            state["progression"]["contract"] = Json();
            return {true, "Contract cancelled.", "contracts"};
        }
        if (contract["progress"].asInt() < contract["goal"].asInt())
            return {false, "The contract is not finished."};
        const double beforePoints = state["points"].asDouble();
        state["points"] = std::min(9e15, beforePoints + contract["points"].asDouble());
        addStatistic("points_earned", state["points"].asDouble() - beforePoints);
        state["progression"]["contracts_completed"] =
            std::min(supplyLimit, amount(state["progression"]["contracts_completed"]) + 1);
        state["progression"]["contract"] = Json();
        return {true, "Contract complete: +" + contract["points"].asString() + " points.", "contracts"};
    }
    if (action == "expedition_start") {
        const int index = args["index"].isInt() ? args["index"].asInt() : -1;
        const Json quote = expeditionQuote(index);
        if (quote.isNull() || quote != args)
            return {false, "That pet changed. Choose it again."};
        auto& trips = state["progression"]["expeditions"];
        const int limit = std::clamp(data.systems["expeditions"].get("slots", 3).asInt(), 1, 3);
        if (trips.size() >= unsigned(limit))
            return {false, "All expedition slots are in use."};
        const int serial = amount(state["progression"]["expedition_serial"]);
        if (serial == supplyLimit)
            return {false, "Expedition limit reached."};
        Json trip = quote;
        trip.removeMember("index");
        trip["serial"] = serial + 1;
        trip["progress"] = 0;
        trips.append(std::move(trip));
        state["inventory_slots"][index] = Json();
        state["progression"]["expedition_serial"] = serial + 1;
        return {true, "Pet sent on expedition.", "expeditions"};
    }
    if (action == "expedition_claim" || action == "expedition_recall") {
        auto& trips = state["progression"]["expeditions"];
        int at = -1;
        for (unsigned i = 0; i < trips.size(); ++i)
            if (args["serial"] == trips[i]["serial"])
                at = int(i);
        if (at < 0)
            return {false, "That expedition has already ended."};
        const Json trip = trips[at];
        const bool claim = action == "expedition_claim";
        if (claim && trip["progress"].asInt() < trip["goal"].asInt())
            return {false, "The expedition is not finished."};
        const Json before = state["inventory_slots"];
        bool fits = giveItem(trip["pet"]);
        if (claim && fits)
            for (const auto& item : trip["loot"])
                if (!giveItem(item)) {
                    fits = false;
                    break;
                }
        if (!fits) {
            state["inventory_slots"] = before;
            return {false, "Make room for your pet" + std::string(claim ? " and its rewards." : ".")};
        }
        if (claim) {
            const double beforePoints = state["points"].asDouble();
            state["points"] = std::min(9e15, beforePoints + trip["points"].asDouble());
            addStatistic("points_earned", state["points"].asDouble() - beforePoints);
            addStatistic("expeditions_completed");
        }
        Json unused;
        trips.removeIndex(unsigned(at), &unused);
        return {true, claim ? "Pet returned with its expedition rewards." : "Pet recalled.", "expeditions"};
    }
    return {false, "Unknown action."};
}

void Game::recordMining(int blocks, int ores, const Json& context) {
    if (blocks <= 0)
        return;
    blocks = std::min(blocks, 5000);
    recordProgress("blocks", blocks, context);
    recordProgress("ores", std::clamp(ores, 0, blocks), context);
    for (auto& trip : state["progression"]["expeditions"])
        if (amount(trip["serial"]) <= amount(context["expedition_serial"]))
            trip["progress"] = std::min(trip["goal"].asInt(), trip["progress"].asInt() + blocks);
}

void Game::recordProgress(const std::string& kind, int count, const Json& context) {
    auto& contract = state["progression"]["contract"];
    if (count <= 0 || !contract.isObject() || contract["kind"] != kind ||
        context["contract_serial"] != contract["serial"])
        return;
    if (kind == "ores" && context["region"] != contract["region"])
        return;
    contract["progress"] =
        std::min(contract["goal"].asInt(), contract["progress"].asInt() + std::min(count, 5000));
}

Json Game::contractChoices() const {
    Json choices(::Json::arrayValue);
    const auto& definitions = data.systems["contracts"];
    if (!definitions.isArray() || definitions.empty())
        return choices;
    const int round = amount(state["progression"]["contracts_completed"]);
    const int stage = 1 + amount(state["rebirth"], 10);
    const auto& region = find(data.systems["regions"], state["progression"]["mine"].asString());
    for (unsigned i = 0; i < 3 && i < definitions.size(); ++i) {
        Json quote = definitions[(unsigned(round) + i) % definitions.size()];
        quote["goal"] = quote["goal"].asInt() * stage;
        quote["points"] = quote["points"].asInt() * stage;
        quote["region"] = region["id"];
        quote["round"] = round;
        choices.append(std::move(quote));
    }
    return choices;
}

std::string Game::contractText(const Json& contract) const {
    const auto goal = compact(contract["goal"].asDouble());
    const auto kind = contract["kind"].asString();
    if (kind == "blocks")
        return "Mine " + goal + " blocks.";
    if (kind == "ores") {
        const auto& region = find(data.systems["regions"], contract["region"].asString());
        return "Mine " + goal + " ore blocks in " + region.get("label", "your mine").asString() + ".";
    }
    return "Finish hatching " + goal + " pets.";
}

Json Game::expeditionQuote(int inventoryIndex) const {
    if (inventoryIndex <= 0 || inventoryIndex >= int(state["inventory_slots"].size()))
        return Json();
    const auto& pet = state["inventory_slots"][inventoryIndex];
    if (!pet.isObject() || pet.get("id", "") != "pet")
        return Json();
    const auto& routes = data.systems["expeditions"]["routes"];
    Json route = find(routes, "quarry");
    for (const auto& candidate : routes) {
        const auto& region = find(data.systems["regions"], candidate["id"].asString());
        if (region["rebirth"].asDouble() > state["rebirth"].asDouble())
            continue;
        for (const auto& species : candidate["species"])
            if (species == pet.get("pet_type", "Rabbit"))
                route = candidate;
    }
    int rarity = 0;
    for (unsigned i = 0; i < data.systems["pets"]["tiers"].size(); ++i)
        if (data.systems["pets"]["tiers"][i]["id"] == pet.get("tier_id", "common"))
            rarity = int(i);
    Json quote;
    quote["index"] = inventoryIndex;
    quote["pet"] = pet;
    quote["region"] = route["id"];
    quote["goal"] = std::max(1, int(std::ceil(route["blocks"].asDouble() *
                                              (1 - std::clamp(amount(pet["level"], 75), 1, 75) * .002))));
    quote["points"] = route["points"].asInt() + rarity * 2;
    quote["loot"].append(route["loot"]);
    return quote;
}

const Json& Game::specialization(const std::string& id) const {
    return find(data.systems["part_specializations"], id);
}

Json Game::specializationModifiers() const {
    Json result;
    for (const char* kind : {"money", "token", "xp", "eggs"})
        result[kind] = 1.0;
    for (const auto& part : state["equipped_parts"]) {
        if (!part.isObject() || !part["specialization"].isString() || !part.get("identified", false).asBool())
            continue;
        const auto& modifiers = specialization(part["specialization"].asString())["modifiers"];
        for (const char* kind : {"money", "token", "xp", "eggs"})
            result[kind] = result[kind].asDouble() + modifiers.get(kind, 0).asDouble();
    }
    for (const char* kind : {"money", "token", "xp", "eggs"})
        result[kind] = std::clamp(result[kind].asDouble(), .55, 1.60);
    return result;
}

} // namespace prison
