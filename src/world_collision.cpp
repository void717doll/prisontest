#include "prison/world.hpp"
#include "prison/guardians.hpp"
#include "world_internal.hpp"
#include <array>

namespace prison {
using namespace world_detail;
namespace {
bool validPosition(Vec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           std::abs(point.x) <= 10000 && std::abs(point.y) <= 10000 && std::abs(point.z) <= 10000;
}
} // namespace

void Mine::setGuardian(const Json& guardian) {
    guardianBoxes_.clear();
    if (guardian.get("active", false).asBool())
        for (const auto& part : guardianParts(guardian))
            guardianBoxes_.push_back({part.low, part.high});
}

bool Mine::solidAt(Vec3 p) const {
    if (!validPosition(p))
        return false;
    if (code(rounded(p)))
        return true;
    for (const auto& b : shell_)
        if (p.x >= b.low.x && p.x < b.high.x && p.y >= b.low.y && p.y < b.high.y && p.z >= b.low.z &&
            p.z < b.high.z)
            return true;
    for (const auto& b : guardianBoxes_)
        if (p.x >= b.low.x && p.x < b.high.x && p.y >= b.low.y && p.y < b.high.y && p.z >= b.low.z &&
            p.z < b.high.z)
            return true;
    return false;
}
bool Mine::solid(Cell c) const {
    return solidAt({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)});
}
Hit Mine::raycast(Vec3 origin, Vec3 direction, float reach) const {
    Hit hit;
    const float length =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length < 1e-6f || !std::isfinite(reach) || reach <= 0)
        return hit;
    if (!validPosition(origin))
        return hit;
    direction = direction * (1 / length);
    reach = std::min(reach, 512.0f);
    Hit scenery;
    auto intersect = [&](const Box& box) {
        float near = 0, far = reach;
        Vec3 normal{};
        for (int a = 0; a < 3; ++a) {
            const float d = axis(direction, a), p = axis(origin, a);
            if (std::abs(d) < 1e-6f) {
                if (p < axis(box.low, a) || p > axis(box.high, a))
                    return;
                continue;
            }
            float entry = (axis(box.low, a) - p) / d, exit = (axis(box.high, a) - p) / d;
            if (entry > exit)
                std::swap(entry, exit);
            if (entry > near) {
                near = entry;
                normal = {};
                setAxis(normal, a, d > 0 ? -1.f : 1.f);
            }
            far = std::min(far, exit);
            if (near > far)
                return;
        }
        if (near <= reach) {
            scenery.hit = true;
            scenery.distance = near;
            scenery.normal = normal;
            scenery.point = origin + direction * near;
            scenery.cell = rounded(scenery.point);
            reach = near;
        }
    };
    for (const auto& box : shell_)
        intersect(box);
    for (const auto& box : guardianBoxes_)
        intersect(box);
    Cell cell = rounded(origin);
    float travelled = 0;
    Vec3 normal{};
    const float infinity = std::numeric_limits<float>::infinity();
    std::array<int, 3> step{};
    std::array<float, 3> tMax{}, tDelta{};
    const int initial[] = {cell.x, cell.y, cell.z};
    for (int a = 0; a < 3; ++a) {
        const float d = axis(direction, a);
        step[a] = d > 0 ? 1 : d < 0 ? -1 : 0;
        tDelta[a] = step[a] ? std::abs(1 / d) : infinity;
        tMax[a] = step[a] ? (initial[a] + (step[a] > 0 ? .5f : -.5f) - axis(origin, a)) / d : infinity;
    }
    for (int iteration = 0; iteration < 1538 && travelled <= reach; ++iteration) {
        if (code(cell)) {
            hit.hit = true;
            hit.cell = cell;
            hit.point = origin + direction * travelled;
            hit.normal = normal;
            hit.distance = travelled;
            hit.block = blockId(cell);
            hit.mineable = !hit.block.empty() && !building_;
            return hit;
        }
        const int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        travelled = tMax[a];
        tMax[a] += tDelta[a];
        normal = {};
        setAxis(normal, a, static_cast<float>(-step[a]));
        if (a == 0)
            cell.x += step[a];
        else if (a == 1)
            cell.y += step[a];
        else
            cell.z += step[a];
    }
    return scenery;
}
bool Mine::collisionBox(const Box& b) const {
    auto overlaps = [&](const Box& o) {
        return b.low.x < o.high.x && b.high.x > o.low.x && b.low.y < o.high.y && b.high.y > o.low.y &&
               b.low.z < o.high.z && b.high.z > o.low.z;
    };
    for (const auto& box : shell_)
        if (overlaps(box))
            return true;
    for (const auto& box : guardianBoxes_)
        if (overlaps(box))
            return true;
    const auto lo = rounded(b.low), hi = rounded(b.high);
    for (int y = std::max(lo.y, minimum_.y); y <= std::min(hi.y, maximum_.y); ++y)
        for (int z = std::max(lo.z, minimum_.z); z <= std::min(hi.z, maximum_.z); ++z)
            for (int x = std::max(lo.x, minimum_.x); x <= std::min(hi.x, maximum_.x); ++x)
                if (code({x, y, z}) &&
                    overlaps({{x - .5f, y - .5f, z - .5f}, {x + .5f, y + .5f, z + .5f}}))
                    return true;
    return false;
}
bool Mine::spaceFree(Vec3 feet, float radius, float height) const {
    return validPosition(feet) &&
           !collisionBox({{feet.x - radius, feet.y, feet.z - radius},
                          {feet.x + radius, feet.y + height, feet.z + radius}});
}
Vec3 Mine::confinePlayer(Vec3 feet, float radius) const {
    if (!validPosition(feet))
        return spawn_;
    radius = std::clamp(radius, .1f, 1.0f);
    feet.x = std::clamp(feet.x, walkMinimum_.x + radius, walkMaximum_.x - radius);
    feet.z = std::clamp(feet.z, walkMinimum_.z + radius, walkMaximum_.z - radius);
    feet.y = std::max(feet.y, walkMinimum_.y);
    return feet;
}
MoveResult Mine::movePlayer(Vec3 feet, Vec3 delta, float radius, float height) const {
    feet = confinePlayer(feet, radius);
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
        return {feet, false};
    radius = std::clamp(radius, .1f, 1.0f);
    height = std::clamp(height, .2f, 3.0f);
    MoveResult result{feet, false};
    constexpr float epsilon = .0001f;
    // sweep against both terrain and scenery to prevent tunnelling during delayed frames.
    for (int a = 0; a < 3; ++a) {
        const float wanted = std::clamp(axis(delta, a), -128.0f, 128.0f);
        if (wanted == 0)
            continue;
        const Box body{{result.position.x - radius, result.position.y, result.position.z - radius},
                       {result.position.x + radius, result.position.y + height, result.position.z + radius}};
        float allowed = wanted;
        auto clip = [&](const Box& obstacle) {
            for (int b = 0; b < 3; ++b)
                if (b != a && (axis(body.high, b) <= axis(obstacle.low, b) + epsilon ||
                               axis(body.low, b) >= axis(obstacle.high, b) - epsilon))
                    return;
            if (wanted > 0 && axis(body.high, a) <= axis(obstacle.low, a) + epsilon)
                allowed = std::min(allowed, std::max(0.0f, axis(obstacle.low, a) - axis(body.high, a)));
            if (wanted < 0 && axis(body.low, a) >= axis(obstacle.high, a) - epsilon)
                allowed = std::max(allowed, std::min(0.0f, axis(obstacle.high, a) - axis(body.low, a)));
        };
        for (const auto& b : shell_)
            clip(b);
        for (const auto& b : guardianBoxes_)
            clip(b);
        Box sweep = body;
        if (wanted > 0)
            setAxis(sweep.high, a, axis(sweep.high, a) + wanted);
        else
            setAxis(sweep.low, a, axis(sweep.low, a) + wanted);
        auto lo = rounded(sweep.low), hi = rounded(sweep.high);
        for (int y = std::max(lo.y, minimum_.y); y <= std::min(hi.y, maximum_.y); ++y)
            for (int z = std::max(lo.z, minimum_.z); z <= std::min(hi.z, maximum_.z); ++z)
                for (int x = std::max(lo.x, minimum_.x); x <= std::min(hi.x, maximum_.x); ++x)
                    if (code({x, y, z}))
                        clip({{x - .5f, y - .5f, z - .5f}, {x + .5f, y + .5f, z + .5f}});
        setAxis(result.position, a, axis(result.position, a) + allowed);
        if (a == 1 && wanted < 0 && allowed > wanted + epsilon)
            result.grounded = true;
    }
    // keep jumps from clearing the platform railing.
    const Vec3 confined = confinePlayer(result.position, radius);
    if (confined.y > result.position.y)
        result.grounded = true;
    result.position = confined;
    if (!result.grounded)
        result.grounded = collisionBox({{result.position.x - radius + epsilon, result.position.y - .015f,
                                         result.position.z - radius + epsilon},
                                        {result.position.x + radius - epsilon, result.position.y,
                                         result.position.z + radius - epsilon}});
    return result;
}
} // namespace prison
