#include "prison/companion.hpp"
#include <array>

namespace prison {
namespace {
float length(Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
} // namespace

void PetCompanion::clear() {
    visible_ = false;
    petId_.clear();
    phase_ = 0;
    stuck_ = 0;
}

void PetCompanion::tick(const Mine& mine, const Json& pet, Vec3 player, float yaw, double seconds) {
    if (mine.resetting() || !pet.isObject() || pet.get("id", "") != "pet") {
        clear();
        return;
    }
    const float dt = std::clamp(float(seconds), 0.f, .1f);
    const auto id = pet.get("uid", "").asString();
    const auto movement = player - player_;
    const bool respawn = !visible_ || id != petId_ || generation_ != mine.generation() ||
                         length(movement) > 8;
    // looking around should let the player see the pet, without making it orbit the camera.
    if (respawn || movement.x * movement.x + movement.z * movement.z > .000001f)
        followYaw_ = yaw;
    player_ = player;
    petId_ = id;
    generation_ = mine.generation();
    phase_ = std::fmod(phase_ + dt * 2.5f, 6.2831853f);

    const Vec3 right{std::cos(followYaw_), 0, -std::sin(followYaw_)};
    const Vec3 back{std::sin(followYaw_), 0, std::cos(followYaw_)};
    const Vec3 eye = player + Vec3{0, 1.62f, 0};
    const std::array<Vec3, 7> offsets = {
        right * 1.25f + back * .85f, right * -1.25f + back * .85f,
        back * 1.6f, back * -1.6f, right * 1.6f, right * -1.6f, Vec3{0, 1.4f, 0}};
    Vec3 target;
    bool found = false;
    for (const auto offset : offsets) {
        const auto spot = mine.confinePlayer(player + offset + Vec3{0, .65f + std::sin(phase_) * .05f, 0},
                                            Radius);
        const auto sight = spot + Vec3{0, Size * .5f, 0} - eye;
        if (length(spot - player) < .9f || !mine.spaceFree(spot, Radius, Size) ||
            mine.raycast(eye, sight, length(sight)).hit)
            continue;
        target = spot;
        found = true;
        break;
    }
    if (!found) {
        visible_ = false;
        return;
    }

    if (respawn || length(feet_ - target) > 8 || !mine.spaceFree(feet_, Radius, Size)) {
        feet_ = target;
        stuck_ = 0;
    } else {
        auto delta = (target - feet_) * (1 - std::exp(-dt * 6));
        const float distance = length(delta);
        if (distance > 14 * dt)
            delta = delta * (14 * dt / distance);
        const auto next = mine.movePlayer(feet_, delta, Radius, Size).position;
        stuck_ = distance > .01f && length(next - feet_) < distance * .2f ? stuck_ + dt : 0;
        // catch up at corners instead of clipping through the quarry or needing a navigation mesh.
        feet_ = stuck_ > .75f ? target : next;
        if (stuck_ > .75f)
            stuck_ = 0;
    }
    const auto towardPlayer = player - feet_;
    const float facing = std::atan2(towardPlayer.x, towardPlayer.z);
    facing_ = respawn ? facing
                      : facing_ + std::remainder(facing - facing_, 6.2831853f) * (1 - std::exp(-dt * 8));
    visible_ = true;
}
} // namespace prison
