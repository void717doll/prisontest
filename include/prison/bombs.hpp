#pragma once
#include "game.hpp"
#include "world.hpp"
#include <deque>
namespace prison {
class Bombs {
  public:
    struct Projectile {
        Vec3 position, velocity;
        float age = 0;
        int tier = 1;
        std::uint64_t generation = 0;
    };
    Result launch(Mine& mine, const Game& game, Vec3 origin, Vec3 direction, int tier);
    void tick(Mine& mine, Game& game, double seconds);
    void clear();
    const std::vector<Projectile>& projectiles() const {
        return projectiles_;
    }

  private:
    struct Blast {
        Cell centre;
        int tier = 1, removed = 0, samples = 0, side = 0;
        double ratio = .25;
        std::uint64_t generation = 0;
        Json context, sample;
        bool started = false;
    };
    float cooldown_ = 0;
    std::vector<Projectile> projectiles_;
    std::deque<Blast> blasts_;
};
} // namespace prison
