#pragma once
#include "game.hpp"

namespace prison {
class Professions {
  public:
    explicit Professions(Game& mining);
    Game& current();
    Game& fishing() { return fishing_; }
    Game& farming() { return farming_; }
    const std::string& active() const { return active_; }
    void load();
    void save();
    Result select(const std::string& name);

  private:
    Game& mining_;
    Data fishingData_, farmingData_;
    Game fishing_, farming_;
    std::string active_ = "mining";
};

constexpr float CourtyardStart = 72;
constexpr float CourtyardEnd = 112;
bool inCourtyard(Vec3 position);
Vec3 plotPosition(int index);
struct OutdoorTarget {
    std::string kind;
    int plot = -1;
    Vec3 point;
};
OutdoorTarget outdoorTarget(Vec3 eye, Vec3 direction);
} // namespace prison
