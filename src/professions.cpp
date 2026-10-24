#include "prison/professions.hpp"
#include <stdexcept>

namespace prison {
Data Data::forProfession(const std::string& name) const {
    if (name == "mining")
        return *this;
    if (name != "fishing" && name != "farming")
        throw std::invalid_argument("Unknown profession");
    Data result = *this;
    result.profession = name;
    const auto& config = systems["professions"][name];
    result.enchants["defs"] = Json(::Json::objectValue);
    result.enchants["order"] = config["order"];
    for (const auto& entry : config["order"]) {
        const auto key = entry.asString();
        Json definition = enchants["defs"][key];
        if (!definition.isObject())
            definition = enchants["defs"]["fortune"];
        for (const auto& field : config["enchants"][key].getMemberNames())
            definition[field] = config["enchants"][key][field];
        result.enchants["defs"][key] = definition;
    }
    Json books(::Json::arrayValue);
    for (const auto& key : systems["enchant_book_whitelist"])
        if (result.enchants["defs"][key.asString()].isObject())
            books.append(key);
    result.systems["enchant_book_whitelist"] = books;
    result.systems["currencies"] = config["currencies"];
    result.systems["endgame"] = Json(::Json::arrayValue);
    result.systems["robots"]["drop_chance"] = 0;
    return result;
}

Professions::Professions(Game& mining)
    : mining_(mining), fishingData_(mining.data.forProfession("fishing")),
      farmingData_(mining.data.forProfession("farming")), fishing_(fishingData_), farming_(farmingData_) {}

Game& Professions::current() {
    return active_ == "fishing" ? fishing_ : active_ == "farming" ? farming_ : mining_;
}

void Professions::load() {
    for (Game* game : {&fishing_, &farming_}) {
        Json saved = mining_.state["professions"][game->data.profession];
        if (saved.isObject()) {
            saved.removeMember("professions");
            saved.removeMember("native_mine");
            saved.removeMember("native_reward_jobs");
            game->loadSnapshot(saved);
        } else
            game->newWorld(mining_.state.get("devmode", false).asBool());
    }
    active_ = mining_.state.get("active_profession", "mining").isString()
                  ? mining_.state.get("active_profession", "mining").asString() : "mining";
    if ((active_ != "fishing" && active_ != "farming") || mining_.inShaft())
        active_ = "mining";
}

void Professions::save() {
    mining_.state["active_profession"] = active_;
    for (Game* game : {&fishing_, &farming_})
        mining_.state["professions"][game->data.profession] = game->snapshot();
}

Result Professions::select(const std::string& name) {
    if (name != "mining" && name != "fishing" && name != "farming")
        return {false, "Unknown profession."};
    if (mining_.inShaft() && name != "mining")
        return {false, "Leave the challenge shaft before switching professions."};
    const Json messages = current().state["message_settings"];
    const Json settings = current().state["settings"];
    current().state.removeMember("_cast");
    active_ = name;
    current().state["message_settings"] = messages;
    current().state["settings"] = settings;
    return {true, "Switched to " + name + "."};
}

bool inCourtyard(Vec3 position) {
    return position.x >= -25 && position.x <= 25 && position.z >= CourtyardStart - 2 &&
           position.z <= CourtyardEnd + 2 && position.y >= -2 && position.y <= 30;
}

Vec3 plotPosition(int index) {
    return {-19.f + (index % 3) * 3, 1.51f, 79.f + (index / 3) * 3};
}

OutdoorTarget outdoorTarget(Vec3 eye, Vec3 direction) {
    OutdoorTarget target;
    if (direction.y >= -.001f)
        return target;
    const float distance = (1.51f - eye.y) / direction.y;
    if (distance <= 0 || distance > 24)
        return target;
    target.point = eye + direction * distance;
    const auto p = target.point;
    if (p.x >= 7 && p.x <= 22 && p.z >= 80 && p.z <= 103 &&
        !(p.x <= 12 && p.z >= 84 && p.z <= 88))
        target.kind = "water";
    if (distance <= 6) {
        for (int i = 0; i < 27; ++i) {
            const auto plot = plotPosition(i);
            if (std::abs(p.x - plot.x) <= 1 && std::abs(p.z - plot.z) <= 1) {
                target.kind = "plot";
                target.plot = i;
            }
        }
        if (std::abs(p.x) <= 3 && p.z >= 104 && p.z <= 109)
            target.kind = "supplies";
    }
    return target;
}
} // namespace prison
