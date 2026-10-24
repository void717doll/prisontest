#pragma once
#include "prison/common.hpp"

namespace prison::world_detail {
inline std::uint64_t hash(std::uint64_t n) {
    n += 0x9e3779b97f4a7c15ULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
inline float random(std::uint64_t n) {
    return static_cast<float>((hash(n) >> 40) * (1.0 / 16777216.0));
}
inline Cell rounded(Vec3 p) {
    return {static_cast<int>(std::floor(p.x + .5f)), static_cast<int>(std::floor(p.y + .5f)),
            static_cast<int>(std::floor(p.z + .5f))};
}
inline float axis(Vec3 p, int i) {
    return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
inline void setAxis(Vec3& p, int i, float v) {
    if (i == 0)
        p.x = v;
    else if (i == 1)
        p.y = v;
    else
        p.z = v;
}
} // namespace prison::world_detail
