#pragma once
#include "common.hpp"
#include <array>

namespace prison {
struct GuardianPart {
    Vec3 low, high;
};
struct GuardianHit {
    int part = -1;
    float distance = 0;
};
std::array<GuardianPart, 6> guardianParts(const Json& guardian);
int guardianWeakPart(const Json& guardian);
std::string guardianWeakName(const Json& guardian);
GuardianHit raycastGuardian(const Json& guardian, Vec3 eye, Vec3 direction, float reach);
} // namespace prison
