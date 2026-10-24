#include "prison/professions.hpp"
#include "items_internal.hpp"
#include <functional>

namespace prison {
namespace {
int amount(const Json& value, int fallback = 0, int maximum = 1000000000) {
    if (!value.isNumeric() || !std::isfinite(value.asDouble()))
        return fallback;
    return int(std::clamp(value.asDouble(), 0.0, double(maximum)));
}
double number(const Json& value, double fallback = 0, double maximum = 9e33) {
    return value.isNumeric() && std::isfinite(value.asDouble())
               ? std::clamp(value.asDouble(), 0.0, maximum)
               : fallback;
}
const Json& definition(const Json& rows, const std::string& id) {
    for (const auto& row : rows)
        if (row["id"] == id)
            return row;
    static const Json empty;
    return empty;
}
double chance(Game& game) {
    return std::generate_canonical<double, 53>(game.random);
}
Json produce(const Game& game, const std::string& kind, const std::string& species, int count) {
    Json item;
    item["id"] = kind;
    item["species"] = species;
    item["count"] = count;
    item["profession"] = game.data.profession;
    return item;
}
}

std::string Game::toolName() const {
    return data.profession == "fishing" ? "Fishing Rod" : data.profession == "farming" ? "Hoe" : "Pickaxe";
}

bool Game::ownsItem(const Json& item) const {
    return !item.isObject() || (item.get("profession", "mining").isString() &&
                               item.get("profession", "mining").asString() == data.profession);
}

bool Game::giveDrop(Json item, bool automatic) {
    if (data.profession != "mining")
        item["profession"] = data.profession;
    return giveItem(std::move(item), automatic);
}

void Game::loadActivity() {
    std::function<void(Json&)> clean = [&](Json& item) {
        if (!item.isObject())
            return;
        if (!ownsItem(item)) {
            item = Json();
            return;
        }
        if (item.isMember("items") && item["items"].isArray())
            for (auto& stored : item["items"])
                clean(stored);
    };
    for (const char* key : {"inventory_slots", "vault_slots", "equipped_crystals", "equipped_parts",
                           "robot_slots", "forge_slots"})
        if (state.isMember(key))
            for (auto& item : state[key])
                clean(item);
    clean(state["pet_data"]["active_pet"]);
    if (data.profession == "mining")
        return;
    state["tutorial"]["status"] = "skipped";
    state["autominer_active"] = false;
    state["activity_count"] = number(state["activity_count"], 0, 9e15);
    const Json old = state["activity"].isObject() ? state["activity"] : Json(::Json::objectValue);
    Json activity;
    activity["introduced"] = old.get("introduced", false) == true;
    activity["guide"] = old.get("guide", false) == true;
    activity["intro_step"] = amount(old["intro_step"], 0, 3);
    activity["bait"] = amount(old["bait"], data.systems["professions"]["fishing"]["bait_starter"].asInt());
    activity["plots_owned"] = 9 + 9 * std::clamp((amount(old["plots_owned"], 9, 27) - 9) / 9, 0, 2);
    const auto& crops = data.systems["professions"]["farming"]["crops"];
    const std::string selected = old["selected_crop"].isString() ? old["selected_crop"].asString() : "wheat";
    activity["selected_crop"] = definition(crops, selected).isObject() ? selected : "wheat";
    for (const auto& crop : crops) {
        const auto key = crop["id"].asString();
        activity["seeds"][key] =
            amount(old["seeds"].isObject() ? old["seeds"][key] : Json(), key == "wheat" ? 9 : 0);
    }
    activity["plots"] = Json(::Json::arrayValue);
    for (int i = 0; i < 27; ++i) {
        Json plot;
        const auto saved = old["plots"].isArray() && i < int(old["plots"].size()) ? old["plots"][i] : Json();
        if (i < activity["plots_owned"].asInt() && saved.isObject() && saved["crop"].isString()) {
            const auto& crop = definition(crops, saved["crop"].asString());
            if (crop.isObject()) {
                plot["crop"] = crop["id"];
                plot["growth"] = number(saved["growth"], 0, crop["seconds"].asDouble());
                plot["watered"] = saved.get("watered", false) == true;
            }
        }
        activity["plots"].append(plot);
    }
    state["activity"] = activity;
    state.removeMember("_cast");
}

double Game::cropProgress(int index) const {
    if (data.profession != "farming" || index < 0 || index >= 27)
        return 0;
    const auto& plot = state["activity"]["plots"][index];
    if (!plot.isObject())
        return 0;
    const auto& crop = definition(data.systems["professions"]["farming"]["crops"], plot["crop"].asString());
    return std::clamp(plot["growth"].asDouble() / std::max(1.0, crop["seconds"].asDouble()), 0.0, 1.0);
}

void Game::tickActivity(double seconds) {
    if (data.profession == "mining" || !std::isfinite(seconds) || seconds <= 0)
        return;
    seconds = std::min(seconds, 86400.0);
    if (state.isMember("_activity_flash"))
        state["_activity_flash"] = std::max(0.0, state["_activity_flash"].asDouble() - seconds);
    if (data.profession == "farming") {
        const auto& crops = data.systems["professions"]["farming"]["crops"];
        for (auto& plot : state["activity"]["plots"]) {
            if (!plot.isObject())
                continue;
            const auto& crop = definition(crops, plot["crop"].asString());
            const double speed = (plot["watered"].asBool() ? 2.0 : 1.0) *
                                 (1 + std::min(100, enchantLevel("efficiency")) * .025);
            plot["growth"] = std::min(crop["seconds"].asDouble(), plot["growth"].asDouble() + seconds * speed);
        }
    }
    auto& cast = state["_cast"];
    if (!cast.isObject())
        return;
    const double remaining = cast["remaining"].asDouble() - seconds;
    if (remaining > 0) {
        cast["remaining"] = remaining;
        return;
    }
    if (cast["phase"] == "waiting" && remaining > -2.5) {
        cast["phase"] = "bite";
        cast["remaining"] = 2.5 + remaining;
        state["_activity_notice"] = "Bite! Right-click to reel in.";
    } else {
        state.removeMember("_cast");
        state["_activity_notice"] = "The fish got away.";
    }
}

Result Game::castFishing(Vec3 point) {
    if (data.profession != "fishing")
        return {false, "Switch to fishing and equip your rod."};
    auto& cast = state["_cast"];
    if (cast.isObject()) {
        if (cast["phase"] != "bite") {
            state.removeMember("_cast");
            return {true, "Line reeled in. Wait for the bobber to dip before reeling."};
        }
        const auto& fishes = data.systems["professions"]["fishing"]["fish"];
        const int level = state["pick_level"].asInt();
        const double quality = std::min(1000, enchantLevel("quality")) / 10000.0;
        double total = 0;
        for (const auto& fish : fishes)
            if (fish["level"].asInt() <= level)
                total += fish["weight"].asDouble() * (1 + fish["level"].asInt() * quality);
        double pick = chance(*this) * total;
        Json selected = fishes[0];
        for (const auto& fish : fishes) {
            if (fish["level"].asInt() > level)
                continue;
            pick -= fish["weight"].asDouble() * (1 + fish["level"].asInt() * quality);
            if (pick < 0) {
                selected = fish;
                break;
            }
        }
        const int fortune = enchantLevel("fortune");
        int count = std::min(99, 1 + fortune / 100 + (chance(*this) < (fortune % 100) / 100.0 ? 1 : 0));
        const bool shoal = chance(*this) < std::min(.05, enchantLevel("shoal") * .00005);
        if (shoal)
            count = std::min(99, count + 5);
        Json caught = produce(*this, "fish", selected["id"].asString(), count);
        const double size = std::min(1.0, chance(*this) + std::min(1000, enchantLevel("quality")) * .0002);
        caught["size_cm"] = selected["min_cm"].asInt() +
                            int(size * (selected["max_cm"].asInt() - selected["min_cm"].asInt()));
        Json inventory = state["inventory_slots"];
        if (!item_detail::insert(inventory, caught, 1)) {
            state.removeMember("_cast");
            state["activity"]["bait"] = state["activity"]["bait"].asInt() + 1;
            return {false, "Inventory full. Fish released and bait returned."};
        }
        state["inventory_slots"] = inventory;
        state.removeMember("_cast");
        state["activity"]["intro_step"] = 3;
        awardActivity(40, selected["xp"].asDouble(), count);
        if (shoal && state["message_settings"].get("visual_enchants", false) == true)
            state["_activity_flash"] = 1.0;
        return {true, "Caught " + std::to_string(count) + " " + selected["label"].asString() +
                          " (" + caught["size_cm"].asString() + " cm). Sell catches at /supplies."};
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.z) || point.x < 7 || point.x > 22 ||
        point.z < 80 || point.z > 103 || (point.x <= 12 && point.z >= 84 && point.z <= 88))
        return {false, "Aim at the pond to cast."};
    auto& activity = state["activity"];
    if (activity["bait"].asInt() <= 0)
        return {false, "Buy fishing bait at /supplies."};
    activity["bait"] = activity["bait"].asInt() - 1;
    activity["intro_step"] = std::max(1, activity["intro_step"].asInt());
    const double wait =
        std::max(.8, (3 + chance(*this) * 5) / (1 + std::min(100, enchantLevel("efficiency")) * .035));
    cast["phase"] = "waiting";
    cast["remaining"] = wait;
    cast["duration"] = wait;
    cast["x"] = point.x;
    cast["z"] = point.z;
    return {true, "Line cast."};
}

Result Game::tendPlot(int index, bool water) {
    if (data.profession != "farming")
        return {false, "Switch to farming to tend these plots."};
    auto& activity = state["activity"];
    if (index < 0 || index >= activity["plots_owned"].asInt())
        return {false, "Expand your farm at /supplies to unlock this plot."};
    auto& plots = activity["plots"];
    auto& plot = plots[index];
    const auto& crops = data.systems["professions"]["farming"]["crops"];
    if (!plot.isObject()) {
        if (water)
            return {false, "Plant a seed with your hoe first."};
        const auto key = activity["selected_crop"].asString();
        const auto& crop = definition(crops, key);
        if (state["pick_level"].asInt() < crop["level"].asInt())
            return {false, "Raise your farming level to plant this crop."};
        if (activity["seeds"][key].asInt() <= 0)
            return {false, "Buy seeds at /supplies."};
        activity["seeds"][key] = activity["seeds"][key].asInt() - 1;
        plot["crop"] = key;
        plot["growth"] = 0.0;
        plot["watered"] = false;
        activity["intro_step"] = std::max(1, activity["intro_step"].asInt());
        return {true, "Planted " + crop["label"].asString() + "."};
    }
    if (water) {
        if (plot["watered"].asBool())
            return {true, "This plot is already watered."};
        plot["watered"] = true;
        activity["intro_step"] = std::max(2, activity["intro_step"].asInt());
        return {true, "Watered. This crop now grows twice as fast."};
    }
    if (cropProgress(index) < 1)
        return {false, "Still growing. Water this plot to speed it up."};
    const auto previousRandom = random;
    const bool reaper = chance(*this) < std::min(.25, enchantLevel("harvester") * .00025);
    const Vec3 centre = plotPosition(index);
    int harvested = 0;
    double xp = 0;
    Json inventory = state["inventory_slots"];
    Json updated = plots;
    Json seeds = activity["seeds"];
    for (int i = 0; i < activity["plots_owned"].asInt(); ++i) {
        const Vec3 pos = plotPosition(i);
        if (i != index && (!reaper || std::abs(pos.x - centre.x) > 3.1f || std::abs(pos.z - centre.z) > 3.1f))
            continue;
        if (cropProgress(i) < 1)
            continue;
        const auto key = plots[i]["crop"].asString();
        const auto& crop = definition(crops, key);
        const int fortune = enchantLevel("fortune");
        const int count = std::min(99, crop["yield"].asInt() + fortune / 100 +
                                          (chance(*this) < (fortune % 100) / 100.0 ? 1 : 0));
        Json output = produce(*this, "crop", key, count);
        output["quality"] = chance(*this) < std::min(.5, enchantLevel("quality") * .0005) ? 2 : 1;
        if (!item_detail::insert(inventory, output, 1)) {
            random = previousRandom;
            return {false, "Free inventory space before harvesting."};
        }
        if (chance(*this) < std::min(.75, enchantLevel("seeds") * .00075))
            seeds[key] = std::min(1000000000, seeds[key].asInt() + 1);
        updated[i] = Json();
        harvested += count;
        xp += crop["xp"].asDouble() * count;
    }
    state["inventory_slots"] = inventory;
    plots = updated;
    activity["seeds"] = seeds;
    activity["intro_step"] = 3;
    awardActivity(20, xp / harvested, harvested);
    if (reaper && state["message_settings"].get("visual_enchants", false) == true)
        state["_activity_flash"] = 1.0;
    return {true, "Harvested " + std::to_string(harvested) + " crops. Sell produce at /supplies."};
}

Result Game::activityAction(const std::string& action, const std::string& value) {
    if (data.profession == "mining")
        return {false, "Choose fishing or farming first.", "professions"};
    auto& activity = state["activity"];
    const auto& config = data.systems["professions"][data.profession];
    if (action == "intro" || action == "skip") {
        activity["introduced"] = true;
        activity["guide"] = action == "intro";
        return {true, action == "skip" ? "Instructions skipped. Find them again in /help." : "",
                "activity:close"};
    }
    if (action == "select_seed" && data.profession == "farming") {
        const auto& crop = definition(config["crops"], value);
        if (!crop.isObject() || state["pick_level"].asInt() < crop["level"].asInt())
            return {false, "That crop is not unlocked yet."};
        activity["selected_crop"] = value;
        return {true, "Selected " + crop["label"].asString() + "."};
    }
    if (action == "starter") {
        const bool fishing = data.profession == "fishing";
        const double price = fishing ? config["bait_price"].asDouble()
                                     : definition(config["crops"], "wheat")["seed_price"].asDouble() * 9;
        bool stocked = state["money"].asDouble() >= price || state["_cast"].isObject();
        if (fishing)
            stocked = stocked || activity["bait"].asInt() > 0;
        else {
            for (const auto& seeds : activity["seeds"])
                stocked = stocked || seeds.asInt() > 0;
            for (const auto& plot : activity["plots"])
                stocked = stocked || plot.isObject();
        }
        for (const char* container : {"inventory_slots", "vault_slots"})
            for (const auto& item : state[container])
                stocked = stocked || (ownsItem(item) && item.get("id", "") == (fishing ? "fish" : "crop"));
        if (stocked)
            return {false, "Use or sell your existing supplies first."};
        if (fishing)
            activity["bait"] = 1;
        else {
            activity["seeds"]["wheat"] = 1;
            activity["selected_crop"] = "wheat";
        }
        return {true, fishing ? "One starter bait added." : "One starter wheat seed added."};
    }
    if (action == "sell") {
        Json inventory = state["inventory_slots"];
        double total = 0;
        const bool fishing = data.profession == "fishing";
        for (auto& item : inventory) {
            if (!item.isObject() || !ownsItem(item) || item.get("id", "") != (fishing ? "fish" : "crop"))
                continue;
            const auto& row = definition(config[fishing ? "fish" : "crops"], item.get("species", "").asString());
            if (!row.isObject())
                continue;
            const double quality = fishing
                ? std::clamp(number(item["size_cm"]) / row["min_cm"].asDouble(), 1.0, 3.0)
                : std::clamp(number(item["quality"], 1), 1.0, 2.0);
            total += row["value"].asDouble() * amount(item["count"], 1, 99) * quality;
            item = Json();
        }
        if (total <= 0)
            return {false, "No produce from this profession to sell."};
        const double multiplier = std::pow(1.8, state["rank_index"].asInt()) *
                                  (1 + enchantLevel("greed") * .1) * boost("money");
        total = std::floor(std::min(data.balance["currency_cap"].asDouble(), total * multiplier));
        const double before = state["money"].asDouble();
        state["money"] = std::min(data.balance["currency_cap"].asDouble(), before + total);
        state["inventory_slots"] = inventory;
        state["_summary_money"] = state.get("_summary_money", 0).asDouble() + state["money"].asDouble() - before;
        if (activity["intro_step"].asInt() >= 3)
            activity["guide"] = false;
        return {true, "Sold for " + compact(state["money"].asDouble() - before) + " " +
                          currencyName("money") + "."};
    }
    if (action == "watering_can" && data.profession == "farming") {
        for (const auto& item : state["inventory_slots"])
            if (item.get("id", "") == "watering_can")
                return {false, "Your watering can is already in your inventory."};
        Json can;
        can["id"] = "watering_can";
        can["count"] = 1;
        return giveDrop(can) ? Result{true, "Watering can added."}
                             : Result{false, "Free an inventory slot first."};
    }
    double price = 0;
    if (action == "bait" && data.profession == "fishing") {
        if (activity["bait"].asInt() > 1000000000 - config["bait_batch"].asInt())
            return {false, "Bait storage is full."};
        price = config["bait_price"].asDouble();
    } else if (action == "seeds" && data.profession == "farming") {
        const auto& crop = definition(config["crops"], value);
        if (!crop.isObject() || state["pick_level"].asInt() < crop["level"].asInt())
            return {false, "That crop is not unlocked yet."};
        if (activity["seeds"][value].asInt() > 999999991)
            return {false, "Seed storage is full."};
        price = crop["seed_price"].asDouble() * 9;
    } else if (action == "plots" && data.profession == "farming") {
        const int expansion = activity["plots_owned"].asInt() / 9 - 1;
        if (expansion >= 2)
            return {false, "All plots are unlocked."};
        if (state["pick_level"].asInt() < config["plot_levels"][expansion].asInt())
            return {false, "Raise your farming level before expanding."};
        price = config["plot_costs"][expansion].asDouble();
    } else
        return {false, "Unknown supply."};
    if (state["money"].asDouble() < price)
        return {false, "Need " + compact(price) + " " + currencyName("money") + "."};
    state["money"] = state["money"].asDouble() - price;
    if (action == "bait")
        activity["bait"] = activity["bait"].asInt() + config["bait_batch"].asInt();
    else if (action == "seeds")
        activity["seeds"][value] = activity["seeds"][value].asInt() + 9;
    else
        activity["plots_owned"] = activity["plots_owned"].asInt() + 9;
    return {true, "Supplies purchased."};
}
} // namespace prison
