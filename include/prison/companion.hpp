#pragma once
#include "world.hpp"

namespace prison {
class PetCompanion {
  public:
    static constexpr float Size = .65f;
    static constexpr float Radius = Size * .71f;

    void clear();
    void tick(const Mine& mine, const Json& pet, Vec3 player, float yaw, double seconds,
              bool sitting = false);
    bool visible() const {
        return visible_;
    }
    Vec3 position() const {
        const float height = scale_.y * std::cos(tilt_) + std::abs(std::sin(tilt_));
        return feet_ + Vec3{0, Size * height * .5f + bounce_, 0};
    }
    float facing() const {
        return facing_;
    }
    Vec3 scale() const {
        return scale_;
    }
    float tilt() const {
        return tilt_;
    }
    bool sitting() const {
        return sitting_;
    }
    bool feeding() const {
        return feedRemaining_ > 0;
    }

  private:
    Vec3 feet_, player_, scale_{1, 1, 1};
    std::string petId_;
    std::uint64_t generation_ = 0;
    double lastFed_ = 0;
    float followYaw_ = 0, facing_ = 0, phase_ = 0, stuck_ = 0, idle_ = 0;
    float feedRemaining_ = 0, tilt_ = 0, bounce_ = 0;
    bool visible_ = false, sitting_ = false, trackingPet_ = false;
};
} // namespace prison
