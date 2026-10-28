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
    sitting_ = false;
    trackingPet_ = false;
    petId_.clear();
    lastFed_ = 0;
    phase_ = 0;
    stuck_ = 0;
    idle_ = 0;
    feedRemaining_ = 0;
    tilt_ = 0;
    bounce_ = 0;
    scale_ = {1, 1, 1};
}

void PetCompanion::tick(const Mine& mine, const Json& pet, Vec3 player, float yaw, double seconds,
                        bool sitting) {
    if (mine.resetting() || !pet.isObject() || pet.get("id", "") != "pet") {
        clear();
        return;
    }
    const float dt = std::clamp(float(seconds), 0.f, .1f);
    const auto id = pet.get("uid", "").asString();
    const auto movement = player - player_;
    const bool changedPet = !trackingPet_ || id != petId_;
    const bool respawn = !visible_ || changedPet || generation_ != mine.generation() ||
                         length(movement) > 8;
    const auto fed = pet.get("last_fed_ts", 0);
    const double lastFed = fed.isNumeric() ? fed.asDouble() : 0;
    feedRemaining_ = std::max(0.f, feedRemaining_ - dt);
    if (changedPet) {
        feedRemaining_ = 0;
        phase_ = 0;
    } else if (lastFed > lastFed_ && lastFed > 0) {
        feedRemaining_ = 1.2f;
    }
    lastFed_ = lastFed;
    trackingPet_ = true;
    // looking around should let the player see the pet, without making it orbit the camera.
    if (respawn || movement.x * movement.x + movement.z * movement.z > .000001f)
        followYaw_ = yaw;
    player_ = player;
    petId_ = id;
    generation_ = mine.generation();
    phase_ = std::fmod(phase_ + dt * 2.5f, 6.2831853f);
    idle_ = !respawn && movement.x * movement.x + movement.z * movement.z < .000001f
                ? std::min(2.f, idle_ + dt)
                : 0;

    Vec3 target;
    bool found = false;
    if (sitting && !respawn && mine.spaceFree(feet_, Radius, Size)) {
        // a seated pet keeps its place while the player walks around the quarry.
        const auto ground = mine.movePlayer(feet_, {0, sitting_ ? -.05f : -3.f, 0}, Radius, Size);
        if (ground.grounded && (!sitting_ || length(ground.position - feet_) < .02f)) {
            target = ground.position;
            found = true;
        }
    }

    const Vec3 right{std::cos(followYaw_), 0, -std::sin(followYaw_)};
    const Vec3 back{std::sin(followYaw_), 0, std::cos(followYaw_)};
    const Vec3 eye = player + Vec3{0, 1.62f, 0};
    const std::array<Vec3, 7> offsets = {
        right * 1.25f + back * .85f, right * -1.25f + back * .85f,
        back * 1.6f, back * -1.6f, right * 1.6f, right * -1.6f, Vec3{0, 1.4f, 0}};
    for (const auto offset : offsets) {
        if (found)
            break;
        auto spot = mine.confinePlayer(player + offset + Vec3{0, .65f + std::sin(phase_) * .05f, 0},
                                       Radius);
        if (!mine.spaceFree(spot, Radius, Size))
            continue;
        if (sitting) {
            const auto ground = mine.movePlayer(spot, {0, -3, 0}, Radius, Size);
            if (!ground.grounded)
                continue;
            spot = ground.position;
        }
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

    if (sitting || respawn || length(feet_ - target) > 8 || !mine.spaceFree(feet_, Radius, Size)) {
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
    const float idleSway = sitting || idle_ > 1 ? std::sin(phase_) : 0;
    const float facing = std::atan2(towardPlayer.x, towardPlayer.z) + idleSway * .06f;
    facing_ = respawn ? facing
                      : facing_ + std::remainder(facing - facing_, 6.2831853f) * (1 - std::exp(-dt * 8));
    sitting_ = sitting;
    scale_ = {1, sitting ? .70f : idle_ > 1 ? .96f : 1.f, 1};
    scale_.y += idleSway * .015f;
    tilt_ = idleSway * .02f;
    bounce_ = 0;
    if (feeding()) {
        const float progress = 1 - feedRemaining_ / 1.2f;
        const float hop = std::abs(std::sin(progress * 9.424778f));
        const float fade = 1 - progress;
        // the bounce and squash stay inside the same collision envelope as the normal pose.
        scale_.y -= ((sitting ? 0.f : .14f) + hop * .08f) * fade;
        tilt_ = tilt_ * (1 - fade) + std::sin(progress * 18.849556f) * .07f * fade;
        bounce_ = hop * .04f * fade;
    }
    visible_ = true;
}
} // namespace prison
