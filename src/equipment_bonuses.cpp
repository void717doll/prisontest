#include "prison/game.hpp"
#include <array>

namespace prison {
namespace {
const Json none;
std::string boostKind(const Json& item) {
    const auto kind = item.get("boost_type", "").asString();
    return kind == "sell" ? "money" : kind == "exp" ? "xp" : kind;
}
} // namespace

const Json& Game::crystalResonance() const {
    const auto& equipped = state["equipped_crystals"];
    if (crystalSlots() < 3 || equipped.size() < 3)
        return none;
    std::array<std::string, 3> kinds;
    int tier = 0;
    for (int i = 0; i < 3; ++i) {
        const auto& crystal = equipped[i];
        if (!ownsItem(crystal) || !crystal.isObject() || crystal.get("id", "") != "crystal" ||
            !crystal.get("identified", false).asBool())
            return none;
        const int next = crystal.get("tier", 1).asInt();
        if (next < data.systems["resonance"]["minimum_tier"].asInt() || (tier && tier != next))
            return none;
        tier = next;
        kinds[i] = boostKind(crystal);
    }
    std::sort(kinds.begin(), kinds.end());
    for (const auto& set : data.systems["resonance"]["sets"]) {
        std::array<std::string, 3> expected;
        for (int i = 0; i < 3; ++i)
            expected[i] = set["kinds"][i].asString();
        std::sort(expected.begin(), expected.end());
        if (kinds == expected)
            return set;
    }
    return none;
}

const Json& Game::petMutation(const Json& pet) const {
    if (!ownsItem(pet) || !pet.isObject() || pet.get("id", "") != "pet" || !pet["mutation"].isString())
        return none;
    for (const auto& trait : data.systems["mutations"]["traits"])
        if (trait["id"] == pet["mutation"])
            return trait;
    return none;
}

double Game::mutationBonus(const std::string& kind) const {
    return std::clamp(petMutation(state["pet_data"]["active_pet"]).get(kind, 0).asDouble(), 0.0, .05);
}

void Game::rollPetMutation(Json& pet) {
    if (pet.get("mutation_rolled", false).asBool())
        return;
    pet["mutation_rolled"] = true;
    const auto& cfg = data.systems["mutations"];
    const auto& traits = cfg["traits"];
    if (traits.empty() || std::generate_canonical<double, 53>(random) >= cfg["chance"].asDouble())
        return;
    const auto index = std::uniform_int_distribution<unsigned>(0, traits.size() - 1)(random);
    pet["mutation"] = traits[index]["id"];
    pet["pet_color"] = traits[index]["color"];
}
} // namespace prison
