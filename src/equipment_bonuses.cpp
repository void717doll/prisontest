#include "prison/game.hpp"

namespace prison {
namespace {
const Json none;
} // namespace

const Json& Game::petMutation(const Json& pet) const {
    if (!pet.isObject() || pet.get("id", "") != "pet" || !pet["mutation"].isString())
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
