#include "prison/bombs.hpp"
namespace prison {
Result Bombs::launch(Mine& mine, const Game& game, Vec3 origin, Vec3 direction, int tier) {
    if (game.shaftComplete() || (game.inShaft() && game.shaftCondition().get("bombs", true) != true))
        return {false, "Bombs are unavailable on this floor."};
    if (mine.resetting())
        return {false, "Wait for the mine to refill."};
    if (game.state.get("autominer_active", false).asBool())
        return {false, "Stop automining before throwing a bomb."};
    if (cooldown_ > 0)
        return {false, "Wait before throwing another bomb."};
    if (projectiles_.size() + blasts_.size() >= 4)
        return {false, "Wait for your other bombs to finish."};
    if (tier < 1 || tier > 20)
        return {false, "Invalid bomb tier."};
    projectiles_.push_back({origin, direction * 24, 0, tier, mine.generation()});
    cooldown_ = 2;
    return {};
}
void Bombs::clear() {
    projectiles_.clear();
    blasts_.clear();
    cooldown_ = 0;
}
void Bombs::tick(Mine& mine, Game& game, double seconds) {
    if (mine.resetting() || game.shaftComplete()) {
        clear();
        return;
    }
    const float dt = std::clamp(float(seconds), 0.f, .1f);
    cooldown_ = std::max(0.f, cooldown_ - dt);
    for (auto it = projectiles_.begin(); it != projectiles_.end();) {
        if (it->generation != mine.generation()) {
            it = projectiles_.erase(it);
            continue;
        }
        Vec3 delta = it->velocity * dt + Vec3{0, -7 * dt * dt, 0};
        float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        auto hit = mine.raycast(it->position, delta, distance);
        it->position = hit.hit ? hit.point - hit.normal * .02f : it->position + delta;
        it->velocity.y -= 14 * dt;
        it->age += dt;
        if (hit.hit || it->age >= 4) {
            Vec3 pos = it->position;
            Blast blast;
            blast.centre = {int(std::floor(pos.x + .5f)), int(std::floor(pos.y + .5f)),
                            int(std::floor(pos.z + .5f))};
            blast.tier = it->tier;
            blast.generation = it->generation;
            blast.ratio = std::uniform_real_distribution<double>(.20, .35)(game.random);
            blast.context = game.captureRewardContext();
            blast.sample = mine.rewardSample();
            blasts_.push_back(std::move(blast));
            it = projectiles_.erase(it);
        } else
            ++it;
    }
    if (blasts_.empty())
        return;
    auto& blast = blasts_.front();
    if (blast.generation != mine.generation()) {
        blasts_.pop_front();
        return;
    }
    if (!blast.started) {
        if (!mine.queueBlast(blast.centre, blast.tier)) {
            blasts_.pop_front();
            return;
        }
        blast.started = true;
    }
    auto removed = mine.advanceBlasts(128);
    if (!removed.empty()) {
        blast.removed += int(removed.size());
        int samples = std::max(1, int(std::floor(blast.removed * blast.ratio)));
        int side = std::min(250, std::max(1, int(std::round(samples * .05))));
        game.awardBomb(int(removed.size()), samples - blast.samples, side - blast.side, blast.context,
                       blast.sample);
        int ores = 0;
        for (const auto& cell : removed)
            if (cell.block.size() >= 4 && cell.block.compare(cell.block.size() - 4, 4, "_ore") == 0)
                ++ores;
        game.recordMining(int(removed.size()), ores, blast.context);
        game.recordShaftMining(int(removed.size()));
        blast.samples = samples;
        blast.side = side;
    }
    if (!mine.pendingBlasts())
        blasts_.pop_front();
}
} // namespace prison
