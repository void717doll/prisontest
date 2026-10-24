#include "prison/world.hpp"
#include "world_internal.hpp"

namespace prison {
using namespace world_detail;

void Mine::buildScenery() {
    plants_.clear();
    const float minX = minimum_.x - .5f, maxX = maximum_.x + .5f, minZ = minimum_.z - .5f,
                maxZ = maximum_.z + .5f;
    const float bottom = minimum_.y - 1.5f, top = maximum_.y + 1.5f;
    const float left = minX - 6, right = maxX + 6, back = minZ - 6, front = spawn_.z + 5.5f;
    const bool shaft = options_["shaft"].isObject();
    const int wallTile = shaft ? TerrainBedrockTile : TerrainMasonryTile;
    shell_ = {{{minX, bottom, minZ}, {maxX, bottom + 1, maxZ}},
              {{minX - 1, bottom, minZ - 1}, {minX, top, maxZ + 1}, wallTile},
              {{maxX, bottom, minZ - 1}, {maxX + 1, top, maxZ + 1}, wallTile},
              {{minX, bottom, minZ - 1}, {maxX, top, minZ}, wallTile},
              {{minX, bottom, maxZ}, {maxX, top, maxZ + 1}, wallTile}};
    auto add = [&](Vec3 low, Vec3 high, int tile) { shell_.push_back({low, high, tile}); };
    const float border = shaft ? 1.f : .75f;
    walkMinimum_ = {left + border, bottom + 1, back + border};
    walkMaximum_ = {right - border, top + 50, front - border};
    if (shaft) {
        add({left, -.5f, back}, {minX - 1, .5f, front}, TerrainBedrockTile);
        add({maxX + 1, -.5f, back}, {right, .5f, front}, TerrainBedrockTile);
        add({minX - 1, -.5f, back}, {maxX + 1, .5f, minZ - 1}, TerrainBedrockTile);
        add({minX - 1, -.5f, maxZ + 1}, {maxX + 1, .5f, front}, TerrainBedrockTile);
        add({spawn_.x - 2.5f, .5f, spawn_.z - 2.5f},
            {spawn_.x + 2.5f, 1.5f, spawn_.z + 2.5f}, TerrainBedrockTile);
        add({left, .5f, back}, {walkMinimum_.x, 3.5f, front}, TerrainBedrockTile);
        add({walkMaximum_.x, .5f, back}, {right, 3.5f, front}, TerrainBedrockTile);
        add({walkMinimum_.x, .5f, back}, {walkMaximum_.x, 3.5f, walkMinimum_.z}, TerrainBedrockTile);
        add({walkMinimum_.x, .5f, walkMaximum_.z}, {walkMaximum_.x, 3.5f, front}, TerrainBedrockTile);
        return;
    }

    const float centre = spawn_.x;
    add({left, -.5f, back}, {minX - 1, 1.5f, front}, TerrainPavingTile);
    add({maxX + 1, -.5f, back}, {right, 1.5f, front}, TerrainPavingTile);
    add({minX - 1, -.5f, back}, {maxX + 1, 1.5f, minZ - 1}, TerrainPavingTile);
    add({minX - 1, -.5f, maxZ + 1}, {centre - 3, 1.5f, front}, TerrainPavingTile);
    add({centre + 3, -.5f, maxZ + 1}, {maxX + 1, 1.5f, front}, TerrainPavingTile);
    add({centre - 3, -.5f, maxZ + 3}, {centre + 3, 1.5f, front}, TerrainPavingTile);
    add({centre - 3, -.5f, maxZ + 1}, {centre + 3, .5f, maxZ + 2}, TerrainTrimTile);
    add({centre - 3, -.5f, maxZ + 2}, {centre + 3, 1, maxZ + 3}, TerrainTrimTile);

    add({left, 1.5f, back}, {walkMinimum_.x, 2.5f, front}, TerrainMasonryTile);
    add({walkMaximum_.x, 1.5f, back}, {right, 2.5f, front}, TerrainMasonryTile);
    add({walkMinimum_.x, 1.5f, back}, {walkMaximum_.x, 2.5f, walkMinimum_.z}, TerrainMasonryTile);
    add({walkMinimum_.x, 1.5f, walkMaximum_.z}, {walkMaximum_.x, 2.5f, front}, TerrainMasonryTile);
    for (float y : {2.6f, 3.2f}) {
        for (float x : {left + .25f, right - .5f})
            add({x, y, back + .5f}, {x + .25f, y + .2f, front - .5f}, TerrainMetalTile);
        for (float z : {back + .25f, front - .5f})
            add({left + .5f, y, z}, {right - .5f, y + .2f, z + .25f}, TerrainMetalTile);
    }
    auto lamp = [&](float x, float z) {
        add({x - .5f, 1.5f, z - .5f}, {x + .5f, 2, z + .5f}, TerrainTrimTile);
        add({x - .25f, 2, z - .25f}, {x + .25f, 4, z + .25f}, TerrainMetalTile);
        add({x - .45f, 4, z - .45f}, {x + .45f, 4.8f, z + .45f}, TerrainLampTile);
        add({x - .6f, 4.8f, z - .6f}, {x + .6f, 5, z + .6f}, TerrainMetalTile);
    };
    const int sideBays = std::max(2, int(std::ceil((front - back - 2) / 8)));
    for (int i = 0; i <= sideBays; ++i) {
        const float z = back + 1 + (front - back - 2) * i / sideBays;
        lamp(left + 1, z);
        lamp(right - 1, z);
    }
    const int endBays = std::max(2, int(std::ceil((right - left - 2) / 8)));
    for (int i = 1; i < endBays; ++i) {
        const float x = left + 1 + (right - left - 2) * i / endBays;
        lamp(x, back + 1);
        lamp(x, front - 1);
    }

    auto support = [&](float x, float z) {
        add({x - .5f, 1.5f, z - .5f}, {x + .5f, 5.5f, z + .5f}, TerrainTimberTile);
        add({x - .6f, 1.5f, z - .6f}, {x + .6f, 2, z + .6f}, TerrainMetalTile);
    };
    const int supports = std::max(2, int(std::ceil((maxZ - minZ + 3) / 9)));
    for (float x : {minX - 1.5f, maxX + 1.5f}) {
        for (int i = 0; i <= supports; ++i)
            support(x, minZ - 1.5f + (maxZ - minZ + 3) * i / supports);
        add({x - .5f, 5.5f, minZ - 2}, {x + .5f, 6, maxZ + 2}, TerrainTimberTile);
    }
    add({minX - 2, 5.5f, minZ - 2}, {maxX + 2, 6, minZ - 1}, TerrainTimberTile);
    const int backSupports = std::max(2, int(std::ceil((maxX - minX + 3) / 9)));
    for (int i = 1; i < backSupports; ++i)
        support(minX - 1.5f + (maxX - minX + 3) * i / backSupports, minZ - 1.5f);
    for (float x : {centre - 4, centre + 4}) {
        add({x - .5f, 1.5f, maxZ + 3}, {x + .5f, 6, maxZ + 4}, TerrainTimberTile);
        add({x - .55f, 1.5f, maxZ + 2.95f}, {x + .55f, 2, maxZ + 4.05f}, TerrainMetalTile);
        add({x - .4f, 3.5f, maxZ + 4}, {x + .4f, 5, maxZ + 4.15f}, smooth_ - 1);
    }
    add({centre - 5, 6, maxZ + 3}, {centre + 5, 6.75f, maxZ + 4}, TerrainTimberTile);

    for (float x : {centre - 8, right - 3}) {
        add({x - 1, 1.5f, spawn_.z + 1}, {x + 1, 3, spawn_.z + 3}, TerrainTimberTile);
        add({x - 1.05f, 2.1f, spawn_.z + .95f},
            {x + 1.05f, 2.3f, spawn_.z + 3.05f}, TerrainMetalTile);
        add({x - .5f, 3, spawn_.z + 1.5f}, {x + .5f, 4, spawn_.z + 2.5f}, TerrainTimberTile);
    }
    for (float x : {centre - 12.5f, centre - 6})
        for (float z : {spawn_.z, spawn_.z + 3})
            add({x - .2f, 1.5f, z - .2f}, {x + .2f, 4.8f, z + .2f}, TerrainTimberTile);
    for (int row = 0; row < 3; ++row) {
        const float z = spawn_.z - .5f + row * 1.4f;
        const float y = row == 1 ? 5.15f : 4.8f;
        add({centre - 13, y, z}, {centre - 5.5f, y + .35f, z + 1.4f}, TerrainTimberTile);
    }
    for (float x : {centre - 11.8f, centre - 10.2f})
        add({x - .2f, 1.5f, spawn_.z + 1}, {x + .2f, 2.6f, spawn_.z + 2.5f}, TerrainTimberTile);
    add({centre - 12.2f, 2.6f, spawn_.z + .8f},
        {centre - 9.8f, 2.9f, spawn_.z + 2.7f}, TerrainTimberTile);
    add({centre - 11.5f, 2.9f, spawn_.z + 1.2f},
        {centre - 10.5f, 3.25f, spawn_.z + 2}, TerrainMetalTile);

    const float trackLeft = centre + 6, trackRight = right - 3, trackZ = maxZ + 3.5f;
    for (float x = trackLeft; x < trackRight; x += 1.5f)
        add({x, 1.5f, trackZ - 1.2f}, {x + .35f, 1.65f, trackZ + 1.2f}, TerrainTimberTile);
    for (float z : {trackZ - .8f, trackZ + .8f})
        add({trackLeft, 1.65f, z - .08f}, {trackRight, 1.85f, z + .08f}, TerrainMetalTile);
    const float cartX = centre + 10;
    for (float x : {cartX - .85f, cartX + .85f})
        for (float z : {trackZ - .9f, trackZ + .9f})
            add({x - .3f, 1.8f, z - .18f}, {x + .3f, 2.4f, z + .18f}, TerrainMetalTile);
    add({cartX - 1.4f, 2.3f, trackZ - 1}, {cartX + 1.4f, 3.2f, trackZ + 1}, TerrainMetalTile);
    add({cartX - 1.1f, 3.2f, trackZ - .7f}, {cartX + 1.1f, 3.5f, trackZ + .7f}, ore_ - 1);
    add({cartX - .6f, 3.5f, trackZ - .4f}, {cartX + .3f, 4, trackZ + .5f}, ore_ - 1);
    buildSurroundings(left, right, back, front);
}

void Mine::buildSurroundings(float left, float right, float back, float front) {
    auto add = [&](Vec3 low, Vec3 high, int tile) { shell_.push_back({low, high, tile}); };
    constexpr float SceneryWidth = 14;
    const float outerLeft = left - SceneryWidth, outerRight = right + SceneryWidth;
    const float outerBack = back - SceneryWidth, outerFront = front + SceneryWidth;
    auto ground = [&](float x1, float z1, float x2, float z2) {
        add({x1, -3.5f, z1}, {x2, -.5f, z2}, TerrainDirtTile);
        add({x1, -.5f, z1}, {x2, .5f, z2}, TerrainGrassTile);
    };
    ground(outerLeft, outerBack, left, outerFront);
    ground(right, outerBack, outerRight, outerFront);
    ground(left, outerBack, right, back);
    ground(left, front, right, outerFront);

    auto pine = [&](float x, float y, float z, float height) {
        add({x - .35f, y, z - .35f}, {x + .35f, y + height - 1, z + .35f}, TerrainTreeTile);
        for (int layer = 0; layer < 4; ++layer) {
            const float half = 2.6f - layer * .6f;
            const float low = y + height - 6 + layer * 1.5f;
            add({x - half, low, z - half}, {x + half, low + 1.6f, z + half}, TerrainLeavesTile);
        }
    };
    const int backBays = int(std::ceil((right - left + 20) / 9));
    for (int i = 0; i <= backBays; ++i) {
        const float x = left - 10 + (right - left + 20) * i / backBays;
        if (i % 2 != 0)
            pine(x + 1, .5f, back - 4, 8 + random(810 + i) * 3);
        else
            pine(x, .5f, front + 6, 8 + random(1180 + i) * 3);
    }
    const int sideBays = int(std::ceil((front - back - 12) / 10));
    for (int side = 0; side < 2; ++side) {
        const float edge = side == 0 ? left : right;
        const float direction = side == 0 ? -1.f : 1.f;
        for (int i = 0; i <= sideBays; ++i) {
            const int seed = 1400 + side * 100 + i;
            const float z = back + 6 + (front - back - 12) * i / sideBays;
            pine(edge + direction * (4 + random(seed + 400) * 2), .5f, z + 1,
                 8 + random(seed + 600) * 4);
            add({edge + direction * 3 - 1, .5f, z - 3.5f},
                {edge + direction * 3 + 1, 2, z - 1.5f}, TerrainLeavesTile);
        }
    }
    for (float x : {left - 5, right + 5}) {
        add({x - 2.4f, .5f, front - 5}, {x + 2.4f, .75f, front - 2}, TerrainTimberTile);
        for (int layer = 0; layer < 2; ++layer)
            for (int row = 0; row < 3 - layer; ++row) {
                const float z = front - 4.7f + row * .9f + layer * .45f;
                const float y = .75f + layer * .8f;
                add({x - 2, y, z}, {x + 2, y + .75f, z + .75f}, TerrainTimberTile);
            }
    }
    int seed = 4000;
    auto flora = [&](float x1, float z1, float x2, float z2) {
        for (float x = x1 + 1.5f; x < x2 - 1.5f; x += 2.5f)
            for (float z = z1 + 1.5f; z < z2 - 1.5f; z += 2.5f) {
                seed += 5;
                if (random(seed) < .45f)
                    continue;
                const bool flower = random(seed + 1) < .3f;
                const int first = flower ? TerrainFlowerTile : TerrainTallGrassTile;
                const int variants = flower ? TerrainFlowerVariants : TerrainTallGrassVariants;
                const int tile = first + int(random(seed + 2) * variants);
                const float height = flower ? .7f : .9f;
                plants_.push_back({{x + random(seed + 3) - .5f, .5f, z + random(seed + 4) - .5f},
                                   height, tile});
            }
    };
    flora(outerLeft, outerBack, left, outerFront);
    flora(right, outerBack, outerRight, outerFront);
    flora(left, outerBack, right, back);
    flora(left, front, right, outerFront);
}
} // namespace prison
