#include "prison/guardians.hpp"
#include "prison/game.hpp"

namespace prison {
namespace {
double savedNumber(const Json& object, const char* key, double maximum) {
    const auto& value = object[key];
    return value.isNumeric() && std::isfinite(value.asDouble()) ? std::clamp(value.asDouble(), 0.0, maximum)
                                                                : 0;
}
} // namespace

std::array<GuardianPart, 6> guardianParts(const Json& guardian) {
    const Vec3 origin{guardian["x"].asFloat(), guardian["y"].asFloat(), guardian["z"].asFloat()};
    std::array<GuardianPart, 6> parts{{{{-.70f, 0, -.38f}, {-.12f, 1, .38f}},
                                       {{.12f, 0, -.38f}, {.70f, 1, .38f}},
                                       {{-.80f, 1, -.45f}, {.80f, 2.25f, .45f}},
                                       {{-.58f, 2.25f, -.52f}, {.58f, 3.25f, .52f}},
                                       {{-1.4f, .85f, -.40f}, {-.85f, 2.15f, .40f}},
                                       {{.85f, .85f, -.40f}, {1.4f, 2.15f, .40f}}}};
    for (auto& part : parts) {
        part.low = part.low + origin;
        part.high = part.high + origin;
    }
    return parts;
}

int guardianWeakPart(const Json& guardian) {
    const int maximum = std::max(1, guardian.get("maximum", 120).asInt());
    const int phase = std::clamp((maximum - guardian.get("health", maximum).asInt()) * 3 / maximum, 0, 2);
    constexpr int parts[] = {4, 5, 3};
    return parts[phase];
}

std::string guardianWeakName(const Json& guardian) {
    const int part = guardianWeakPart(guardian);
    return part == 4 ? "Left arm" : part == 5 ? "Right arm" : "Head";
}

GuardianHit raycastGuardian(const Json& guardian, Vec3 eye, Vec3 direction, float reach) {
    GuardianHit hit{-1, reach};
    if (!guardian.get("active", false).asBool())
        return hit;
    const auto parts = guardianParts(guardian);
    const float origin[] = {eye.x, eye.y, eye.z}, ray[] = {direction.x, direction.y, direction.z};
    for (int i = 0; i < int(parts.size()); ++i) {
        const float low[] = {parts[i].low.x, parts[i].low.y, parts[i].low.z};
        const float high[] = {parts[i].high.x, parts[i].high.y, parts[i].high.z};
        float near = 0, far = hit.distance;
        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(ray[axis]) < 1e-6f) {
                if (origin[axis] < low[axis] || origin[axis] > high[axis])
                    intersects = false;
                continue;
            }
            const float a = (low[axis] - origin[axis]) / ray[axis];
            const float b = (high[axis] - origin[axis]) / ray[axis];
            near = std::max(near, std::min(a, b));
            far = std::min(far, std::max(a, b));
            if (near > far)
                intersects = false;
        }
        if (intersects && near <= hit.distance)
            hit = {i, near};
    }
    return hit;
}

void Game::loadGuardian() {
    const auto& cfg = data.systems["guardians"];
    const Json old = state["guardian"].isObject() ? state["guardian"] : Json(::Json::objectValue);
    Json guardian;
    guardian["maximum"] = cfg["health"];
    guardian["health"] = int(savedNumber(old, "health", cfg["health"].asInt()));
    guardian["remaining"] = savedNumber(old, "remaining", cfg["duration"].asDouble());
    guardian["cooldown"] = savedNumber(old, "cooldown", cfg["cooldown"].asDouble());
    guardian["defeated"] = int(savedNumber(old, "defeated", 1000000000));
    bool valid = true;
    for (const char* axis : {"x", "y", "z"}) {
        valid = valid && old[axis].isNumeric() && std::isfinite(old[axis].asDouble()) &&
                std::abs(old[axis].asDouble()) <= 1000;
        guardian[axis] = valid ? old[axis].asDouble() : 0.0;
    }
    guardian["active"] = valid && old["active"].isBool() && old["active"].asBool() &&
                         guardian["health"].asInt() > 0 && guardian["remaining"].asDouble() > 0 && !inShaft();
    state["guardian"] = guardian;
}

bool Game::spawnGuardian(Vec3 position, bool development) {
    const auto& cfg = data.systems["guardians"];
    auto& guardian = state["guardian"];
    if (inShaft() || state["autominer_active"].asBool() || guardian["active"].asBool())
        return false;
    if (development) {
        if (!state["devmode"].asBool())
            return false;
    } else if (state["pick_level"].asInt() < cfg["required_pickaxe_level"].asInt() ||
               guardian["cooldown"].asDouble() > 0 ||
               std::generate_canonical<double, 53>(random) >= cfg["chance"].asDouble())
        return false;
    guardian["x"] = position.x;
    guardian["y"] = position.y;
    guardian["z"] = position.z;
    guardian["maximum"] = cfg["health"];
    guardian["health"] = cfg["health"];
    guardian["remaining"] = cfg["duration"];
    guardian["cooldown"] = cfg["cooldown"];
    guardian["active"] = true;
    return true;
}

bool Game::tickGuardian(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return false;
    auto& guardian = state["guardian"];
    guardian["cooldown"] = std::max(0.0, guardian["cooldown"].asDouble() - seconds);
    if (!guardian["active"].asBool())
        return false;
    guardian["remaining"] = std::max(0.0, guardian["remaining"].asDouble() - seconds);
    if (guardian["remaining"].asDouble() > 0)
        return false;
    guardian["active"] = false;
    return true;
}

Result Game::strikeGuardian(int part) {
    auto& guardian = state["guardian"];
    if (!guardian["active"].asBool() || state["autominer_active"].asBool() ||
        part != guardianWeakPart(guardian))
        return {false};
    guardian["health"] = std::max(0, guardian["health"].asInt() - 1);
    if (guardian["health"].asInt() > 0)
        return {};
    guardian["active"] = false;
    guardian["defeated"] = std::min(1000000000, guardian["defeated"].asInt() + 1);
    const auto& cfg = data.systems["guardians"];
    const int points = cfg.get("points", 3).asInt();
    state["points"] = std::min(9e15, state["points"].asDouble() + points);
    return {true, "Stone Guardian defeated. +" + std::to_string(points) + " points."};
}
} // namespace prison
