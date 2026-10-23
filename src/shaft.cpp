#include "prison/game.hpp"

namespace prison {
namespace {
constexpr int depthLimit = 1000000000;
int counter(const Json& value, int maximum = depthLimit) {
    return value.isNumeric() && std::isfinite(value.asDouble())
               ? int(std::clamp(value.asDouble(), 0.0, double(maximum)))
               : 0;
}
} // namespace

bool Game::inShaft() const {
    return state["shaft"].isObject() && state["shaft"].get("active", false) == true;
}

bool Game::shaftComplete() const {
    return inShaft() && state["shaft"]["progress"].asInt() >= state["shaft"]["floor"]["goal"].asInt();
}

const Json& Game::shaftCondition() const {
    if (inShaft())
        for (const auto& condition : data.systems["shaft"]["conditions"])
            if (condition["id"] == state["shaft"]["floor"]["condition"])
                return condition;
    static const Json empty;
    return empty;
}

Json Game::shaftFloor(int depth) const {
    const auto& config = data.systems["shaft"];
    depth = std::clamp(depth, 1, depthLimit);
    const int band = int(std::floor(std::log2(double(depth))));
    const auto& conditions = config["conditions"];
    const auto& condition = conditions[(depth - 1) % int(conditions.size())];
    Json places(::Json::arrayValue);
    for (const auto& region : data.systems["regions"])
        if (region["rebirth"].asDouble() <= state["rebirth"].asDouble())
            places.append(region);
    const auto& place = places[((depth - 1) / int(conditions.size())) % int(places.size())];
    const auto& low = data.mine["mine_min"];
    const auto& high = data.mine["mine_max"];
    const int volume = (high["x"].asInt() - low["x"].asInt() + 1) *
                       (high["z"].asInt() - low["z"].asInt() + 1) * (high["y"].asInt() - low["y"].asInt());
    Json floor;
    floor["depth"] = depth;
    floor["condition"] = condition["id"];
    floor["region"] = place["id"];
    floor["region_label"] = place["label"];
    floor["goal"] =
        std::max(1, std::min({config["base_blocks"].asInt() + band * config["blocks_per_band"].asInt(),
                              config["max_blocks"].asInt(), volume / 2}));
    floor["dig_scale"] = condition["dig_scale"].asDouble() * std::min(2.0, 1 + band * .10);
    const int rewardScale = depth % config["milestone_interval"].asInt() == 0 ? 2 : 1;
    floor["points"] = config["clear_points"].asInt() * rewardScale;
    return floor;
}

void Game::loadShaft() {
    Json old = state["shaft"].isObject() ? state["shaft"] : Json(::Json::objectValue);
    Json value;
    value["best_depth"] = counter(old["best_depth"]);
    value["floors_cleared"] = counter(old["floors_cleared"]);
    value["active"] = false;
    if (old.get("active", false) == true && old["floor"].isObject() && old["return_mine"].isObject()) {
        const int depth = std::max(1, counter(old["floor"]["depth"]));
        Json floor = shaftFloor(depth);
        // keep an accepted floor's terms when balance tables change.
        for (const auto& region : data.systems["regions"])
            if (region["id"] == old["floor"]["region"]) {
                floor["region"] = region["id"];
                floor["region_label"] = region["label"];
            }
        for (const auto& condition : data.systems["shaft"]["conditions"])
            if (condition["id"] == old["floor"]["condition"])
                floor["condition"] = condition["id"];
        const auto& toughness = old["floor"]["dig_scale"];
        if (toughness.isNumeric() && std::isfinite(toughness.asDouble()))
            floor["dig_scale"] = std::clamp(toughness.asDouble(), 1.0, 4.0);
        floor["goal"] = std::max(1, counter(old["floor"]["goal"], 2000));
        floor["points"] = counter(old["floor"]["points"], 14) + counter(old["floor"]["materials"], 4);
        value["floor"] = std::move(floor);
        value["active"] = true;
        value["progress"] = counter(old["progress"], value["floor"]["goal"].asInt());
        value["return_mine"] = old["return_mine"];
        value["return_player"] = old["return_player"].isObject() ? old["return_player"] : Json();
        state["autominer_active"] = false;
        state["_stress"]["active"] = false;
    }
    state["shaft"] = std::move(value);
}

Result Game::shaftAction(const std::string& action, const Json& args) {
    auto& shaft = state["shaft"];
    if (action == "start") {
        if (inShaft())
            return {false, "You are already in the shaft."};
        if (state["total_prestige"].asDouble() < data.systems["shaft"]["required_prestige"].asInt())
            return {false, "Reach your first prestige to enter the shaft."};
        if (state.get("autominer_active", false) == true)
            return {false, "Stop automining before entering the shaft."};
        if (!args["mine"].isObject())
            return {false, "Your mine could not be preserved."};
        if (shaft["best_depth"].asInt() >= depthLimit)
            return {false, "Shaft depth limit reached."};
        shaft["return_mine"] = args["mine"];
        shaft["return_player"] = args["player"];
        shaft["floor"] = shaftFloor(shaft["best_depth"].asInt() + 1);
        shaft["progress"] = 0;
        shaft["active"] = true;
        state["_stress"]["active"] = false;
        return {true, "Entered the challenge shaft.", "", true};
    }
    if (!inShaft())
        return {false, "You are not in the shaft."};
    if (action == "next") {
        if (!shaftComplete())
            return {false, "Finish this floor before descending."};
        const int depth = shaft["floor"]["depth"].asInt();
        if (depth >= depthLimit)
            return {false, "Shaft depth limit reached."};
        shaft["floor"] = shaftFloor(depth + 1);
        shaft["progress"] = 0;
        return {true, "Descended to floor " + std::to_string(depth + 1) + ".", "", true};
    }
    if (action == "leave") {
        state["mine_state"] = shaft["return_mine"];
        state["native_player"] = shaft["return_player"];
        shaft["active"] = false;
        shaft.removeMember("return_mine");
        shaft.removeMember("return_player");
        shaft.removeMember("floor");
        shaft.removeMember("progress");
        return {true, "Returned to your mine."};
    }
    return {false, "Unknown shaft action."};
}

bool Game::recordShaftMining(int blocks) {
    if (!inShaft() || shaftComplete() || blocks <= 0)
        return false;
    auto& shaft = state["shaft"];
    const auto& floor = shaft["floor"];
    shaft["progress"] = std::min(floor["goal"].asInt(), shaft["progress"].asInt() + std::min(blocks, 5000));
    if (!shaftComplete())
        return false;
    shaft["best_depth"] = std::max(shaft["best_depth"].asInt(), floor["depth"].asInt());
    shaft["floors_cleared"] = std::min(depthLimit, shaft["floors_cleared"].asInt() + 1);
    state["points"] = std::min(9e15, state["points"].asDouble() + floor["points"].asDouble());
    return true;
}
} // namespace prison
