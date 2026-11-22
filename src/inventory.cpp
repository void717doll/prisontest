#include "items_internal.hpp"
#include <cctype>

namespace prison {
using namespace item_detail;

namespace {
int limit(const Json& item) {
    if (id(item) == "pet_food")
        return 99;
    if (item.get("identified", false).asBool() || !item.get("uid", "").asString().empty())
        return 1;
    const std::string name = id(item);
    if (name == "pickaxe" || name == "pet" || name == "pouch" || name == "robot")
        return 1;
    if (name == "crystal" || name == "crystal_dust" || name == "pick_part" || name == "enchant_book" ||
        name == "liquid_xp")
        return 64;
    return 99;
}

Json signature(Json item) {
    // older food rewards carry metadata that does not change their effect.
    if (id(item) == "pet_food") {
        Json food;
        food["id"] = "pet_food";
        return food;
    }
    item.removeMember("count");
    item.removeMember("instance_id");
    if (id(item) == "crystal" || id(item) == "pick_part") {
        item["tier"] = item.get("tier", 1).asInt();
        item["identified"] = item.get("identified", false);
    }
    return item;
}

bool matches(const Json& a, const Json& b) {
    return a.isObject() && b.isObject() && limit(a) > 1 && limit(b) > 1 && signature(a) == signature(b);
}

int insertAvailable(Json& slots, const Json& item, int start = 0) {
    int remaining = count(item);
    for (unsigned i = static_cast<unsigned>(start); i < slots.size() && remaining > 0; ++i)
        if (matches(slots[i], item)) {
            int take = std::min(remaining, std::max(0, limit(item) - count(slots[i])));
            slots[i]["count"] = count(slots[i]) + take;
            remaining -= take;
        }
    for (unsigned i = static_cast<unsigned>(start); i < slots.size() && remaining > 0; ++i)
        if (slots[i].isNull()) {
            int take = std::min(remaining, limit(item));
            slots[i] = item;
            slots[i]["count"] = take;
            remaining -= take;
        }
    return remaining;
}

int vaultPage(const Json& state, const std::string& name) {
    if (name == "vault")
        return 0;
    if (name.rfind("vault:", 0) != 0)
        return -1;
    const auto text = name.substr(6);
    if (text.empty() || text.size() > 2 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
        return -1;
    const int page = std::stoi(text);
    return page < int(state["vault_slots"].size()) / VaultPageSize ? page : -1;
}

Json container(const Json& state, const std::string& name) {
    if (name == "inventory")
        return state["inventory_slots"];
    if (name == "robots")
        return state["robot_slots"];
    const int page = vaultPage(state, name);
    if (page >= 0) {
        Json rows(::Json::arrayValue);
        for (int i = 0; i < VaultPageSize; ++i)
            rows.append(state["vault_slots"][page * VaultPageSize + i]);
        return rows;
    }
    if (name == "crystals")
        return state["equipped_crystals"];
    if (name == "hatchery")
        return state["pet_data"]["hatchery"];
    Json rows(::Json::arrayValue);
    if (name == "parts") {
        for (const auto* part : partNames)
            rows.append(state["equipped_parts"][part]);
        return rows;
    }
    if (name == "pet") {
        rows.append(state["pet_data"]["active_pet"]);
        return rows;
    }
    if (name.rfind("pouch:", 0) == 0) {
        int index = -1;
        try {
            index = std::stoi(name.substr(6));
        } catch (const std::exception&) {
        }
        if (index <= 0 || index >= int(state["inventory_slots"].size()))
            return rows;
        const auto& pouch = state["inventory_slots"][index];
        if (id(pouch) != "pouch")
            return rows;
        rows = pouch.get("items", rows);
        rows.resize(std::clamp(pouch.get("slots", 18).asInt(), 1, 54));
    }
    return rows;
}

void setContainer(Json& state, const std::string& name, const Json& rows) {
    if (name == "inventory")
        state["inventory_slots"] = rows;
    else if (name == "robots")
        state["robot_slots"] = rows;
    else if (vaultPage(state, name) >= 0) {
        const int offset = vaultPage(state, name) * VaultPageSize;
        for (int i = 0; i < VaultPageSize; ++i)
            state["vault_slots"][offset + i] = rows[i];
    } else if (name == "crystals")
        state["equipped_crystals"] = rows;
    else if (name == "hatchery")
        state["pet_data"]["hatchery"] = rows;
    else if (name == "parts")
        for (int i = 0; i < 3; i++)
            state["equipped_parts"][partNames[i]] = rows[i];
    else if (name == "pet")
        state["pet_data"]["active_pet"] = rows[0];
    else if (name.rfind("pouch:", 0) == 0)
        state["inventory_slots"][std::stoi(name.substr(6))]["items"] = rows;
}

bool equipment(const std::string& name) {
    return name == "crystals" || name == "parts" || name == "pet" || name == "robots";
}

bool accepted(const Game& game, const std::string& name, int index, const Json& item) {
    if (item.isNull())
        return true;
    if (name == "inventory")
        return index > 0 && id(item) != "pickaxe";
    if (name == "parts")
        return index >= 0 && index < 3 && id(item) == "pick_part" && item.get("identified", false).asBool() &&
               item.get("slot", "").asString() == partNames[index] &&
               item.get("tier", 1).asInt() <= game.state.get("parts_prestige", 0).asInt() + 1;
    if (name == "crystals")
        return index >= 0 && index < game.crystalSlots() && id(item) == "crystal" &&
               item.get("identified", false).asBool();
    if (name == "pet")
        return index == 0 && id(item) == "pet";
    if (name == "robots") {
        const char* kinds[] = {"money", "xp", "token"};
        return index >= 0 && index < 3 && id(item) == "robot" &&
               rewardKind(item.get("boost_type", "").asString()) == kinds[index];
    }
    if (vaultPage(game.state, name) >= 0)
        return id(item) != "pickaxe";
    if (name.rfind("pouch:", 0) == 0)
        return id(item) != "pickaxe" && id(item) != "pouch";
    return false;
}
} // namespace

namespace item_detail {
int count(const Json& item) {
    return item.isObject() ? std::max(1, item.get("count", 1).asInt()) : 0;
}

std::string id(const Json& item) {
    return item.isObject() ? item.get("id", "").asString() : "";
}

bool insert(Json& slots, const Json& item, int start) {
    if (!slots.isArray() || !item.isObject() || count(item) <= 0 || count(item) > 100000)
        return false;
    Json result = slots;
    if (insertAvailable(result, item, start) > 0)
        return false;
    slots = std::move(result);
    return true;
}

void consume(Json& slots, int index, int amount) {
    int remaining = count(slots[index]) - amount;
    if (remaining <= 0)
        slots[index] = Json();
    else
        slots[index]["count"] = remaining;
}

std::string uid(Game& game, const std::string& prefix) {
    return prefix + "-" + std::to_string(game.random());
}

std::string rewardKind(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "sell" || value == "money" || value == "cash" || value == "sellboost" ||
        value == "moneyboost" || value == "cashboost")
        return "money";
    if (value == "exp" || value == "xp" || value == "expboost" || value == "xpboost")
        return "xp";
    if (value == "token" || value == "tokenboost")
        return "token";
    return "";
}
} // namespace item_detail

int Game::vaultPages() const {
    return std::clamp(int(state["vault_slots"].size()) / VaultPageSize, 1, MaxVaultPages);
}

Json Game::vaultSlots(int page) const {
    return container(state, "vault:" + std::to_string(page));
}

std::string Game::vaultName(int page) const {
    return page >= 0 && page < vaultPages() ? state["vault_names"][page].asString() : "";
}

double Game::nextVaultCost() const {
    const auto& config = data.systems["vault"];
    return std::min(data.balance.get("currency_cap", 9e33).asDouble(),
                    config.get("page_base_cost", 1e14).asDouble() *
                        std::pow(config.get("page_cost_multiplier", 6).asDouble(), vaultPages() - 1));
}

void Game::returnStoredItems() {
    if (!state.isMember("forge_slots") || !state["forge_slots"].isArray())
        return;
    bool pending = false;
    for (auto& item : state["forge_slots"]) {
        if (!item.isObject())
            continue;
        if (giveItem(item)) {
            item = Json();
            continue;
        }
        if (insert(state["vault_slots"], item))
            item = Json();
        else
            pending = true;
    }
    if (!pending)
        state.removeMember("forge_slots");
}

bool Game::giveItem(Json item, bool automatic) {
    if (!item.isObject() || id(item).empty() || id(item) == "pickaxe" || !item.get("count", 1).isNumeric() ||
        item.get("count", 1).asDouble() < 1 || item.get("count", 1).asDouble() > 100000)
        return false;
    if (id(item) == "pouch" && !item.isMember("uid"))
        item["uid"] = uid(*this, "pouch");
    std::string action = automatic ? filterAction(state["cleaner_settings"], item) : "keep";
    if (action == "salvage" && salvage(*this, item))
        return true;
    if (action == "apply" && apply(*this, item))
        return true;
    Json slots = state["inventory_slots"];
    const int page = state["cleaner_settings"].get("vault", 1).asInt() - 1;
    const bool toVault = action == "vault" && page >= 0 && page < vaultPages();
    Json vault;
    if (toVault) {
        vault = vaultSlots(page);
        const int remaining = insertAvailable(vault, item);
        if (remaining == 0) {
            setContainer(state, "vault:" + std::to_string(page), vault);
            return true;
        }
        item["count"] = remaining;
    }
    if (!insert(slots, item, 1))
        return false;
    state["inventory_slots"] = slots;
    if (toVault)
        setContainer(state, "vault:" + std::to_string(page), vault);
    return true;
}

Result Game::transfer(const std::string& sourceName, int index, const std::string& targetName, int target) {
    const int sourcePage = vaultPage(state, sourceName), targetPage = vaultPage(state, targetName);
    const std::string from = sourcePage >= 0 ? "vault:" + std::to_string(sourcePage) : sourceName;
    const std::string to = targetPage >= 0 ? "vault:" + std::to_string(targetPage) : targetName;
    if ((from == "hatchery" && to != "inventory") || (to == "hatchery" && from != "inventory"))
        return fail("Move hatchery items through your inventory.");

    if (from == "inventory" && (index < 0 || index >= int(state["inventory_slots"].size())))
        return fail("Invalid inventory slot.");
    if (from == "inventory" && to == "pet" && id(state["inventory_slots"][index]) == "pet_food") {
        Json args;
        args["index"] = index;
        return itemAction("feed", args);
    }
    if (from == "inventory" && to == "robots" && id(state["inventory_slots"][index]) == "robot_fuel") {
        Json args;
        args["index"] = target;
        args["fuel_slot"] = index;
        return itemAction("robot_refuel", args);
    }
    if (from == to && target < 0)
        return success();
    if (from == "inventory" && index == 0)
        return fail("Your pickaxe stays in the first slot.");
    if (to == "inventory" && target == 0 && from == "inventory") {
        Json args;
        args["index"] = index;
        const auto name = id(state["inventory_slots"][index]);
        return itemAction(name == "enchant_book" ? "book" : "xp", args);
    }
    if (from == "hatchery" && to == "inventory") {
        Json args;
        args["index"] = index;
        args["target"] = target;
        return itemAction("claim", args);
    }
    if (to == "hatchery" && from == "inventory") {
        Json args;
        args["index"] = index;
        args["target"] = target;
        return itemAction("hatch", args);
    }
    Json source = container(state, from), destination = from == to ? source : container(state, to);
    if (to == "crystals")
        destination.resize(crystalSlots());
    if (index < 0 || index >= int(source.size()) || !source[index].isObject())
        return fail("That slot is empty.");
    if (from == to && target == index)
        return success();
    if (to.rfind("pouch:", 0) == 0 && from == "inventory" && std::to_string(index) == to.substr(6))
        return fail("A pouch cannot contain itself.");
    if (from.rfind("pouch:", 0) == 0 && to == "inventory" && std::to_string(target) == from.substr(6))
        return fail("Choose another inventory slot.");
    Json held = source[index];
    if (target < 0) {
        if (equipment(to)) {
            for (unsigned i = 0; i < destination.size(); ++i)
                if (destination[i].isNull() && accepted(*this, to, int(i), held)) {
                    target = int(i);
                    break;
                }
        } else {
            if (!accepted(*this, to, to == "inventory" ? 1 : 0, held))
                return fail("That item does not fit here.");
            if (!insert(destination, held, to == "inventory" ? 1 : 0))
                return fail("Inventory is full.");
            source[index] = Json();
            setContainer(state, from, source);
            setContainer(state, to, destination);
            return success();
        }
    }
    if (target < 0 || target >= int(destination.size()) || !accepted(*this, to, target, held))
        return fail("That item does not fit here.");
    Json previous = destination[target];
    if (!previous.isNull() && !accepted(*this, from, index, previous))
        return fail("The equipped item cannot return to that slot.");
    if (equipment(to) && count(held) > 1)
        return fail("Identify this item first.");
    if (matches(held, previous)) {
        int amount = std::min(count(held), std::max(0, limit(held) - count(previous)));
        if (amount == 0)
            return fail("Stack is full.");
        destination[target]["count"] = count(previous) + amount;
        consume(source, index, amount);
    } else {
        destination[target] = held;
        source[index] = previous;
    }
    if (from == to) {
        source[target] = destination[target];
        setContainer(state, from, source);
    } else {
        if (from.rfind("pouch:", 0) == 0 && to == "inventory") {
            destination[std::stoi(from.substr(6))]["items"] = source;
            setContainer(state, to, destination);
        } else {
            setContainer(state, from, source);
            setContainer(state, to, destination);
        }
    }
    return success();
}

} // namespace prison
