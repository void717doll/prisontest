#pragma once
#include "world.hpp"

namespace prison {
class PetCompanion {
  public:
    static constexpr float Size = .65f;
    static constexpr float Radius = Size * .71f;

    void clear();
    void tick(const Mine& mine, const Json& pet, Vec3 player, float yaw, double seconds);
    bool visible() const {
        return visible_;
    }
    Vec3 position() const {
        return feet_ + Vec3{0, Size * .5f, 0};
    }
    float facing() const {
        return facing_;
    }

  private:
    Vec3 feet_, player_;
    std::string petId_;
    std::uint64_t generation_ = 0;
    float followYaw_ = 0, facing_ = 0, phase_ = 0, stuck_ = 0;
    bool visible_ = false;
};
} // namespace prison
