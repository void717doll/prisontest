#include "prison/world.hpp"
#include "prison/guardians.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <set>
#include <stdexcept>
#include <string_view>

namespace prison {
namespace {
constexpr int ChunkSize = 5;
constexpr float SceneryWidth = 20;
constexpr std::uint32_t Missing = std::numeric_limits<std::uint32_t>::max();
constexpr const char* Codes = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr Cell Neighbours[] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
constexpr float Lights[] = {.80f, .80f, .65f, .65f, 1.0f, .50f};
constexpr float Faces[6][4][3] = {
    {{-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}},
    {{.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, .5f, -.5f}, {.5f, .5f, -.5f}},
    {{.5f, -.5f, .5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}},
    {{-.5f, -.5f, -.5f}, {-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f}},
    {{-.5f, .5f, .5f}, {.5f, .5f, .5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f}},
    {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, -.5f, .5f}, {-.5f, -.5f, .5f}}};
std::uint64_t nextMeshRevision() {
    // revisions must stay unique when the renderer switches between mine instances.
    static std::atomic<std::uint64_t> revision{1};
    return revision.fetch_add(1, std::memory_order_relaxed);
}
std::uint64_t hash(std::uint64_t n) {
    n += 0x9e3779b97f4a7c15ULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
float random(std::uint64_t n) {
    return static_cast<float>((hash(n) >> 40) * (1.0 / 16777216.0));
}
Cell rounded(Vec3 p) {
    return {static_cast<int>(std::floor(p.x + .5f)), static_cast<int>(std::floor(p.y + .5f)),
            static_cast<int>(std::floor(p.z + .5f))};
}
float axis(Vec3 p, int i) {
    return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
void setAxis(Vec3& p, int i, float v) {
    if (i == 0)
        p.x = v;
    else if (i == 1)
        p.y = v;
    else
        p.z = v;
}
void face(std::vector<Vertex>& vertices, Vec3 centre, int side, int tile, Vec3 size = {1, 1, 1}) {
    constexpr int triangles[] = {0, 1, 2, 0, 2, 3};
    constexpr float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    // Half a texel inset avoids neighbouring tiles bleeding at the edge.
    const float inset = .5f / 16.0f;
    for (int i : triangles) {
        const auto& p = Faces[side][i];
        const float u =
            (tile % TerrainAtlasColumns + inset + uv[i][0] * (1 - 2 * inset)) / TerrainAtlasColumns;
        const float v = (tile / TerrainAtlasColumns + inset + uv[i][1] * (1 - 2 * inset)) / TerrainAtlasRows;
        const float light = tile == TerrainLampTile ? 1.f : Lights[side];
        vertices.push_back({centre.x + p[0] * size.x, centre.y + p[1] * size.y,
                            centre.z + p[2] * size.z, u, v, light, light, light});
    }
}
void boxMesh(std::vector<Vertex>& vertices, Vec3 low, Vec3 high, int tile) {
    // only the surfaces need vertices, including partial blocks at the entry platform.
    for (int side = 0; side < 6; ++side) {
        int faceTile = tile;
        if (tile == TerrainTimberTile && side >= 4)
            faceTile = TerrainTimberEndTile;
        else if (tile == TerrainGrassTile && side != 4)
            faceTile = side == 5 ? TerrainDirtTile : TerrainGrassSideTile;
        const int a = side < 2 ? 0 : side < 4 ? 2 : 0;
        const int b = side < 4 ? 1 : 2;
        const Vec3 extent = high - low;
        for (int v = 0; v < std::ceil(axis(extent, b)); ++v)
            for (int u = 0; u < std::ceil(axis(extent, a)); ++u) {
                Vec3 size = extent, centre = (low + high) * .5f;
                setAxis(size, a, std::min(1.f, axis(extent, a) - u));
                setAxis(size, b, std::min(1.f, axis(extent, b) - v));
                setAxis(centre, a, axis(low, a) + u + axis(size, a) * .5f);
                setAxis(centre, b, axis(low, b) + v + axis(size, b) * .5f);
                face(vertices, centre, side, faceTile, size);
            }
    }
}
bool integer(const Json& v, double low, double high) {
    if (!v.isNumeric())
        return false;
    const double n = v.asDouble();
    return std::isfinite(n) && std::floor(n) == n && n >= low && n <= high;
}
} // namespace

MineBounds mineBounds(const Data& data, std::int64_t totalPrestige) {
    const auto& cfg = data.mine;
    const double progress =
        double(std::clamp<std::int64_t>(totalPrestige, 0, MineSizePrestigeCap)) / MineSizePrestigeCap;
    const int arena = cfg.get("arena_half", 50).asInt();
    const int floor = cfg.get("arena_y_min", -100).asInt();
    const int edge = std::max(0, arena - 13);
    auto interpolate = [progress](int from, int to) {
        return int(std::floor(from + (to - from) * progress + .5));
    };
    const auto& low = cfg["mine_min"];
    const auto& high = cfg["mine_max"];
    MineBounds bounds{
        {interpolate(low.get("x", -12).asInt(), -edge),
         interpolate(low.get("y", -8).asInt(), std::max(floor, low.get("y", -8).asInt() - 24)) + 1,
         interpolate(low.get("z", -12).asInt(), -edge)},
        {interpolate(high.get("x", 12).asInt(), edge), high.get("y", -1).asInt(),
         interpolate(high.get("z", 12).asInt(), edge)}};
    if (totalPrestige >= MineSizePrestigeCap) {
        const int width = std::min(100, arena * 2 + 1);
        bounds.minimum.x = int(std::floor((bounds.minimum.x + bounds.maximum.x) / 2.0)) - width / 2;
        bounds.minimum.z = int(std::floor((bounds.minimum.z + bounds.maximum.z) / 2.0)) - width / 2;
        bounds.maximum.x = bounds.minimum.x + width - 1;
        bounds.maximum.z = bounds.minimum.z + width - 1;
        // the reference's 50-block depth includes the unmineable floor.
        bounds.minimum.y = std::max(floor, bounds.maximum.y - 49) + 1;
    }
    return bounds;
}

Mine::Mine(const Data& data) : data_(data) {
    resetDelay_ = std::clamp(data.mine.get("regen_delay", 10).asDouble(), 0.0, 30.0);
    resetRatio_ = std::clamp(data.balance.get("mine_auto_reset_ratio", .3).asDouble(), 0.0, 1.0);
    resize(mineBounds(data, 0));
    buildShell();
}

bool Mine::resize(const MineBounds& bounds) {
    if (!cells_.empty() && bounds.minimum.x == minimum_.x && bounds.minimum.y == minimum_.y &&
        bounds.minimum.z == minimum_.z && bounds.maximum.x == maximum_.x && bounds.maximum.y == maximum_.y &&
        bounds.maximum.z == maximum_.z)
        return false;
    minimum_ = bounds.minimum;
    maximum_ = bounds.maximum;
    width_ = maximum_.x - minimum_.x + 1;
    height_ = maximum_.y - minimum_.y + 1;
    depth_ = maximum_.z - minimum_.z + 1;
    if (width_ < 2 || width_ > 100 || height_ < 2 || height_ > 50 || depth_ < 2 || depth_ > 100)
        throw std::runtime_error("Invalid mine dimensions");
    const auto& cfg = data_.mine;
    spawn_ = {float(std::floor((minimum_.x + maximum_.x) / 2.0)), cfg["spawn"].get("y", 2).asFloat(),
              maximum_.z + cfg["spawn"].get("z", 18).asFloat() - cfg["mine_max"].get("z", 12).asFloat()};
    const auto count = static_cast<std::size_t>(width_) * height_ * depth_;
    cells_.resize(count);
    pocketCells_.resize(count);
    livePositions_.resize(count, Missing);
    live_.reserve(count);
    chunkX_ = (width_ + ChunkSize - 1) / ChunkSize;
    chunkY_ = (height_ + ChunkSize - 1) / ChunkSize;
    chunkZ_ = (depth_ + ChunkSize - 1) / ChunkSize;
    chunks_.resize(static_cast<std::size_t>(chunkX_) * chunkY_ * chunkZ_);
    dirtyFlags_.resize(chunks_.size(), false);
    return true;
}
bool Mine::inside(Cell c) const {
    return c.x >= minimum_.x && c.x <= maximum_.x && c.y >= minimum_.y && c.y <= maximum_.y &&
           c.z >= minimum_.z && c.z <= maximum_.z;
}
std::size_t Mine::index(Cell c) const {
    return static_cast<std::size_t>(c.x - minimum_.x) + (c.z - minimum_.z) * width_ +
           (c.y - minimum_.y) * width_ * depth_;
}
Cell Mine::position(std::size_t i) const {
    return {minimum_.x + static_cast<int>(i % width_), minimum_.y + static_cast<int>(i / (width_ * depth_)),
            minimum_.z + static_cast<int>((i / width_) % depth_)};
}
std::uint8_t Mine::code(Cell c) const {
    return inside(c) ? cells_[index(c)] : 0;
}
std::string Mine::blockId(Cell c) const {
    const int n = code(c);
    return n > 0 && n <= static_cast<int>(data_.mine["blocks"].size())
               ? data_.mine["blocks"][n - 1]["id"].asString()
               : "";
}
const Json& Mine::definition(Cell c) const {
    static const Json empty;
    const int n = code(c);
    return n > 0 && n <= static_cast<int>(data_.mine["blocks"].size()) ? data_.mine["blocks"][n - 1] : empty;
}
Json Mine::rewardSample() const {
    Json sample;
    sample["smooth"] = data_.mine["blocks"][smooth_ - 1]["id"];
    sample["ore"] = data_.mine["blocks"][ore_ - 1]["id"];
    sample["ore_ratio"] = oreRatio_;
    return sample;
}
void Mine::configure(const Json& input) {
    nextOptions_ = Json(::Json::objectValue);
    nextOptions_["region"] = "quarry";
    if (input.isObject() && integer(input["size_prestige"], 0, MineSizePrestigeCap))
        nextOptions_["size_prestige"] = input["size_prestige"];
    if (input.isObject())
        for (const auto& row : data_.systems["regions"])
            if (input["region"] == row["id"])
                nextOptions_["region"] = row["id"];
    for (const auto& row : data_.systems["mine_upgrades"]) {
        const auto id = row["id"].asString();
        const Json value = input.isObject() && input["upgrades"].isObject() ? input["upgrades"][id] : Json();
        nextOptions_["upgrades"][id] = integer(value, 0, row["levels"].size()) ? value.asInt() : 0;
    }
    if (input.isObject() && input["shaft"].isObject() && integer(input["shaft"]["depth"], 1, 1000000000))
        for (const auto& condition : data_.systems["shaft"]["conditions"])
            if (condition["id"] == input["shaft"]["condition"]) {
                nextOptions_["shaft"]["depth"] = input["shaft"]["depth"];
                nextOptions_["shaft"]["condition"] = condition["id"];
            }
}
int Mine::upgradeLevel(const std::string& id) const {
    return options_["upgrades"].get(id, 0).asInt();
}
void Mine::reset(int rank, std::int64_t prestige, std::uint64_t seed) {
    rank = std::clamp(rank, 0, 25);
    prestige = std::max<std::int64_t>(0, prestige);
    if (building_ && !restoring_ && rank == rank_ && prestige == prestige_ && seed == 0 &&
        options_ == nextOptions_)
        return;
    if (nextOptions_.isNull())
        configure(Json());
    const auto sizePrestige =
        nextOptions_["shaft"].isObject()
            ? 0
            : nextOptions_.get("size_prestige", Json(::Json::Int64(prestige))).asInt64();
    beginReset(rank, prestige, seed, mineBounds(data_, sizePrestige));
}

void Mine::beginReset(int rank, std::int64_t prestige, std::uint64_t seed, const MineBounds& bounds) {
    const bool resized = resize(bounds);
    const bool newTheme = themeName_.empty() || rank_ != rank ||
                          options_["region"] != nextOptions_["region"] ||
                          options_["shaft"] != nextOptions_["shaft"];
    options_ = nextOptions_;
    Json region;
    for (const auto& row : data_.systems["regions"])
        if (row["id"] == options_["region"])
            region = row;
    layout_ = region.get("layout", "scattered").asString();
    rank_ = rank;
    prestige_ = prestige;
    seed_ =
        seed ? seed
             : hash(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    ++generation_;
    building_ = true;
    restoring_ = false;
    restore_ = Json();
    restorePalette_.clear();
    cursor_ = total_ = virtualMined_ = 0;
    resetRemaining_ = -1;
    blasts_.clear();
    guardianBoxes_.clear();
    live_.clear();
    std::fill(cells_.begin(), cells_.end(), 0);
    std::fill(livePositions_.begin(), livePositions_.end(), Missing);
    dirty_.clear();
    std::fill(dirtyFlags_.begin(), dirtyFlags_.end(), false);
    for (auto& chunk : chunks_) {
        chunk.vertices.clear();
        chunk.revision = nextMeshRevision();
    }
    smooth_ = 1;
    ore_ = 2;
    const std::string key = "tier_" + std::string(1, static_cast<char>('a' + rank_));
    const auto& defs = data_.mine["blocks"];
    for (unsigned i = 0; i < defs.size() && i < 61; ++i) {
        const auto id = defs[i].get("id", "").asString();
        if (id == key + "_smooth")
            smooth_ = static_cast<int>(i) + 1;
        if (id == key + "_ore")
            ore_ = static_cast<int>(i) + 1;
    }
    oreRatio_ = static_cast<float>(
        (rank_ == 25 ? 24.0 : std::min(24.0, 6.0 + rank_ * .16 + prestige_ * .16)) / 100.0);
    ore_ = 2 * std::min(25, rank_ + region.get("ore_offset", 0).asInt()) + 2;
    oreRatio_ = std::clamp(
        oreRatio_ + region.get("ore_bonus", 0).asFloat() + upgradeLevel("ore_density") * .02f, 0.f, .50f);
    if (options_["shaft"].isObject())
        for (const auto& condition : data_.systems["shaft"]["conditions"])
            if (condition["id"] == options_["shaft"]["condition"])
                oreRatio_ = std::clamp(oreRatio_ + condition["ore_bonus"].asFloat(), 0.f, .65f);
    if (newTheme) {
        const auto& themes = data_.systems["discoveries"]["themes"];
        themeName_ = themes.isArray() && rank_ < int(themes.size()) ? themes[rank_].asString() : "Stone";
        const auto tile = defs[smooth_ - 1].get("tile", "").asString();
        const auto colorAt = tile.find('#');
        Color tint = colorAt == std::string::npos ? Color{} : hexColor(tile.substr(colorAt, 7));
        sky_ = {.025f + tint.r * .035f, .03f + tint.g * .035f, .045f + tint.b * .035f, 1};
        if (options_["region"] != "quarry") {
            themeName_ = region.get("label", "Mine").asString() + " · " + themeName_;
            tint = hexColor(region.get("color", "#808080").asString());
            sky_ = {.025f + tint.r * .035f, .03f + tint.g * .035f, .045f + tint.b * .035f, 1};
        }
        if (options_["shaft"].isObject())
            themeName_ = "Shaft " + options_["shaft"]["depth"].asString() + " · " + themeName_;
        else
            sky_ = {.10f + tint.r * .06f, .13f + tint.g * .06f, .18f + tint.b * .06f, 1};
    }
    if (resized || newTheme)
        buildShell();
    generateDiscoveries();
}

void Mine::generateDiscoveries() {
    std::fill(pocketCells_.begin(), pocketCells_.end(), 0);
    if (options_["shaft"].isObject())
        return;
    const auto& config = data_.systems["discoveries"];
    if (width_ < 9 || depth_ < 9 || height_ < 6)
        return;
    if (random(seed_ + 811) >= std::clamp(config.get("pocket_chance", 0).asDouble(), 0.0, 1.0))
        return;
    Cell centre{minimum_.x + 2 + int(hash(seed_ + 812) % (width_ - 4)),
                minimum_.y + 1 + int(hash(seed_ + 813) % (height_ - 3)),
                minimum_.z + 2 + int(hash(seed_ + 814) % (depth_ - 4))};
    const int count = std::clamp(config.get("pocket_blocks", 5).asInt() + upgradeLevel("pocket_size"), 1, 7);
    pocketCells_[index(centre)] = 1;
    for (int i = 0; i < count - 1; ++i)
        pocketCells_[index(
            {centre.x + Neighbours[i].x, centre.y + Neighbours[i].y, centre.z + Neighbours[i].z})] = 1;

}

int Mine::takePocketDrops(int limit) {
    const int count = std::min(pendingPocketDrops_, std::max(0, limit));
    pendingPocketDrops_ -= count;
    if (restoring_)
        restore_["pending_pocket_drops"] = pendingPocketDrops_;
    return count;
}

bool Mine::step(std::size_t cellBudget, std::size_t chunkBudget) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
    std::size_t done = 0;
    if (building_ && cursor_ < cells_.size()) {
        // Keep the source snapshot string alive instead of copying it per cell.
        const char* begin = nullptr;
        const char* end = nullptr;
        if (restoring_)
            restore_["cells"].getString(&begin, &end);
        while (cursor_ < cells_.size() && done < cellBudget) {
            const auto i = cursor_++;
            const Cell c = position(i);
            std::uint8_t tile = 0;
            if (restoring_) {
                const auto n = std::string_view(Codes).find(begin[i]);
                if (n > 0)
                    tile = restorePalette_[n - 1];
            } else if (pocketCells_[i])
                tile = static_cast<std::uint8_t>(ore_);
            else {
                float chance = oreRatio_;
                if (layout_ == "seams") {
                    const float rich = std::min(1.f, oreRatio_ * 3);
                    chance = (c.x - minimum_.x + (c.z - minimum_.z) / 3) % 5 == 0
                                 ? rich
                                 : (oreRatio_ - rich * .2f) / .8f;
                } else if (layout_ == "bands")
                    chance *= (c.y - minimum_.y) % 3 == 0 ? 2.f : .5f;
                tile = static_cast<std::uint8_t>(random(seed_ ^ hash(i)) < chance ? ore_ : smooth_);
            }
            cells_[i] = tile;
            if (tile) {
                livePositions_[i] = static_cast<std::uint32_t>(live_.size());
                live_.push_back(static_cast<std::uint32_t>(i));
            }
            ++done;
            if ((done & 63) == 0 && std::chrono::steady_clock::now() >= deadline)
                break;
        }
        if (cursor_ == cells_.size()) {
            total_ = live_.size();
            if (restoring_) {
                total_ = restore_["total_blocks"].asUInt64();
                virtualMined_ = restore_.get("virtual_mined", 0).asUInt64();
                resetRemaining_ = restore_.get("reset_remaining", -1).asDouble();
            }
            markAllDirty();
        }
    }
    if (!building_ || cursor_ == cells_.size()) {
        std::size_t built = 0;
        while (!dirty_.empty() && built < chunkBudget && std::chrono::steady_clock::now() < deadline) {
            auto chunk = dirty_.front();
            dirty_.pop_front();
            dirtyFlags_[chunk] = false;
            buildChunk(chunk);
            ++built;
        }
    }
    if (building_ && cursor_ == cells_.size() && dirty_.empty()) {
        building_ = restoring_ = false;
        restore_ = Json();
        restorePalette_.clear();
        scheduleReset();
        return true;
    }
    return false;
}
void Mine::markDirty(Cell c) {
    if (!inside(c))
        return;
    const auto n = static_cast<std::size_t>((c.x - minimum_.x) / ChunkSize) +
                   ((c.z - minimum_.z) / ChunkSize) * chunkX_ +
                   ((c.y - minimum_.y) / ChunkSize) * chunkX_ * chunkZ_;
    if (!dirtyFlags_[n]) {
        dirtyFlags_[n] = true;
        dirty_.push_back(n);
    }
}
void Mine::markAllDirty() {
    for (std::size_t i = 0; i < chunks_.size(); ++i)
        if (!dirtyFlags_[i]) {
            dirtyFlags_[i] = true;
            dirty_.push_back(i);
        }
}
void Mine::buildChunk(std::size_t n) {
    auto& mesh = chunks_[n];
    mesh.vertices.clear();
    const Cell low = {minimum_.x + static_cast<int>(n % chunkX_) * ChunkSize,
                      minimum_.y + static_cast<int>(n / (chunkX_ * chunkZ_)) * ChunkSize,
                      minimum_.z + static_cast<int>((n / chunkX_) % chunkZ_) * ChunkSize};
    const Cell high = {std::min(maximum_.x, low.x + ChunkSize - 1),
                       std::min(maximum_.y, low.y + ChunkSize - 1),
                       std::min(maximum_.z, low.z + ChunkSize - 1)};
    mesh.minimum = {low.x - .5f, low.y - .5f, low.z - .5f};
    mesh.maximum = {high.x + .5f, high.y + .5f, high.z + .5f};
    for (int y = low.y; y <= high.y; ++y)
        for (int z = low.z; z <= high.z; ++z)
            for (int x = low.x; x <= high.x; ++x) {
                const Cell c{x, y, z};
                const int tile = code(c);
                if (!tile)
                    continue;
                for (int side = 0; side < 6; ++side) {
                    const auto d = Neighbours[side];
                    const Cell other{x + d.x, y + d.y, z + d.z};
                    if (!solid(other)) {
                        int atlasTile = pocketCells_[index(c)] ? TerrainPocketTile : tile - 1;
                        face(mesh.vertices,
                             {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, side,
                             atlasTile);
                    }
                }
            }
    mesh.revision = nextMeshRevision();
}
void Mine::buildShell() {
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
    walkMinimum_ = {left + 1, bottom + 1, back + 1};
    walkMaximum_ = {right - 1, top + 50, front - 1};
    if (shaft) {
        add({left, -.5f, back}, {minX - 1, .5f, front}, TerrainBedrockTile);
        add({maxX + 1, -.5f, back}, {right, .5f, front}, TerrainBedrockTile);
        add({minX - 1, -.5f, back}, {maxX + 1, .5f, minZ - 1}, TerrainBedrockTile);
        add({minX - 1, -.5f, maxZ + 1}, {maxX + 1, .5f, front}, TerrainBedrockTile);
        add({spawn_.x - 2.5f, .5f, spawn_.z - 2.5f},
            {spawn_.x + 2.5f, 1.5f, spawn_.z + 2.5f}, TerrainBedrockTile);
        add({left, .5f, back}, {left + 1, 3.5f, front}, TerrainBedrockTile);
        add({right - 1, .5f, back}, {right, 3.5f, front}, TerrainBedrockTile);
        add({left + 1, .5f, back}, {right - 1, 3.5f, back + 1}, TerrainBedrockTile);
        add({left + 1, .5f, front - 1}, {right - 1, 3.5f, front}, TerrainBedrockTile);
    } else {
        const float centre = spawn_.x;
        add({left, -.5f, back}, {minX - 1, 1.5f, front}, TerrainPavingTile);
        add({maxX + 1, -.5f, back}, {right, 1.5f, front}, TerrainPavingTile);
        add({minX - 1, -.5f, back}, {maxX + 1, 1.5f, minZ - 1}, TerrainPavingTile);
        add({minX - 1, -.5f, maxZ + 1}, {centre - 3, 1.5f, front}, TerrainPavingTile);
        add({centre + 3, -.5f, maxZ + 1}, {maxX + 1, 1.5f, front}, TerrainPavingTile);
        add({centre - 3, -.5f, maxZ + 3}, {centre + 3, 1.5f, front}, TerrainPavingTile);
        add({centre - 3, -.5f, maxZ + 1}, {centre + 3, .5f, maxZ + 2}, TerrainTrimTile);
        add({centre - 3, -.5f, maxZ + 2}, {centre + 3, 1, maxZ + 3}, TerrainTrimTile);

        add({left, 1.5f, back}, {left + .75f, 2.5f, front}, TerrainMasonryTile);
        add({right - .75f, 1.5f, back}, {right, 2.5f, front}, TerrainMasonryTile);
        add({left + .75f, 1.5f, back}, {right - .75f, 2.5f, back + .75f}, TerrainMasonryTile);
        add({left + .75f, 1.5f, front - .75f}, {right - .75f, 2.5f, front}, TerrainMasonryTile);
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
        buildScenery(left, right, back, front);
    }
    shellMesh_.vertices.clear();
    for (const auto& box : shell_)
        boxMesh(shellMesh_.vertices, box.low, box.high, box.tile);
    shellMesh_.revision = nextMeshRevision();
}
void Mine::buildScenery(float left, float right, float back, float front) {
    auto add = [&](Vec3 low, Vec3 high, int tile) { shell_.push_back({low, high, tile}); };
    const float outerLeft = left - SceneryWidth, outerRight = right + SceneryWidth;
    const float outerBack = back - SceneryWidth, outerFront = front + SceneryWidth;
    walkMinimum_.x = outerLeft + 1;
    walkMinimum_.z = outerBack + 1;
    walkMaximum_.x = outerRight - 1;
    walkMaximum_.z = outerFront - 1;
    auto ground = [&](float x1, float z1, float x2, float z2) {
        add({x1, -3.5f, z1}, {x2, -.5f, z2}, TerrainDirtTile);
        add({x1, -.5f, z1}, {x2, .5f, z2}, TerrainGrassTile);
    };
    ground(outerLeft, outerBack, left, outerFront);
    ground(right, outerBack, outerRight, outerFront);
    ground(left, outerBack, right, back);
    ground(left, front, right, outerFront);

    add({outerLeft, -3.5f, outerBack}, {outerLeft + 1, 3.5f, outerFront}, TerrainRockTile);
    add({outerRight - 1, -3.5f, outerBack}, {outerRight, 3.5f, outerFront}, TerrainRockTile);
    add({outerLeft + 1, -3.5f, outerBack}, {outerRight - 1, 3.5f, outerBack + 1}, TerrainRockTile);
    add({outerLeft + 1, -3.5f, outerFront - 1}, {outerRight - 1, 3.5f, outerFront}, TerrainRockTile);

    auto pine = [&](float x, float y, float z, float height) {
        add({x - .35f, y, z - .35f}, {x + .35f, y + height - 1, z + .35f}, TerrainTimberTile);
        for (int layer = 0; layer < 4; ++layer) {
            const float half = 2.6f - layer * .6f;
            const float low = y + height - 6 + layer * 1.5f;
            add({x - half, low, z - half}, {x + half, low + 1.6f, z + half}, TerrainLeavesTile);
        }
    };
    auto outcrop = [&](float x, float z, float width, float height) {
        add({x - width, .5f, z - width}, {x + width, height * .45f, z + width}, TerrainRockTile);
        add({x - width * .8f, height * .45f, z - width * .85f},
            {x + width * .7f, height * .8f, z + width * .7f}, TerrainRockTile);
        add({x - width * .55f, height * .8f, z - width * .55f},
            {x + width * .4f, height, z + width * .4f}, TerrainRockTile);
        add({x - width * .55f, height, z - width * .55f},
            {x + width * .4f, height + .2f, z + width * .4f}, TerrainGrassTile);
    };
    const int backBays = int(std::ceil((right - left + 20) / 9));
    for (int i = 0; i <= backBays; ++i) {
        const float x = left - 10 + (right - left + 20) * i / backBays;
        const float height = 7 + random(230 + i) * 6;
        outcrop(x, back - 12, 4.5f + random(450 + i) * 1.5f, height);
        if (i % 2 == 0)
            pine(x - .5f, height + .2f, back - 12.5f, 7 + random(690 + i) * 3);
        else
            pine(x + 1, .5f, back - 4, 8 + random(810 + i) * 3);
        outcrop(x, front + 12, 3 + random(940 + i), 3 + random(1060 + i) * 3);
        if (i % 2 == 0)
            pine(x, .5f, front + 6, 8 + random(1180 + i) * 3);
    }
    const int sideBays = int(std::ceil((front - back - 12) / 10));
    for (int side = 0; side < 2; ++side) {
        const float edge = side == 0 ? left : right;
        const float direction = side == 0 ? -1.f : 1.f;
        for (int i = 0; i <= sideBays; ++i) {
            const int seed = 1400 + side * 100 + i;
            const float z = back + 6 + (front - back - 12) * i / sideBays;
            outcrop(edge + direction * 12, z, 4 + random(seed + 200) * 2, 4 + random(seed) * 5);
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
        outcrop(x, back + 3, 1.8f, 2.8f);
    }
}
void Mine::setGuardian(const Json& guardian) {
    guardianBoxes_.clear();
    if (guardian.get("active", false).asBool())
        for (const auto& part : guardianParts(guardian))
            guardianBoxes_.push_back({part.low, part.high});
}

bool Mine::solidAt(Vec3 p) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        return false;
    if (p.x > 10000 || p.x < -10000 || p.y > 10000 || p.y < -10000 || p.z > 10000 || p.z < -10000)
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
std::string Mine::dig(Cell c) {
    if (building_ || !inside(c))
        return "";
    const auto i = index(c);
    const auto id = blockId(c);
    if (id.empty())
        return "";
    if (pocketCells_[i]) {
        pendingPocketDrops_ = std::min(1000000000, pendingPocketDrops_ + 1);
        pocketCells_[i] = 0;
    }
    cells_[i] = 0;
    const auto p = livePositions_[i], last = live_.back();
    live_[p] = last;
    livePositions_[last] = p;
    live_.pop_back();
    livePositions_[i] = Missing;
    markDirty(c);
    for (auto d : Neighbours)
        markDirty({c.x + d.x, c.y + d.y, c.z + d.z});
    scheduleReset();
    return id;
}
bool Mine::nextTarget(Cell& cell) const {
    if (building_ || live_.empty())
        return false;
    cell = position(live_.back());
    return true;
}
double Mine::minedRatio() const {
    return total_ ? std::clamp(1.0 - (static_cast<double>(live_.size()) - virtualMined_) / total_, 0.0, 1.0)
                  : 0;
}
void Mine::scheduleReset() {
    if (resetRemaining_ < 0 && resetDelay_ > 0 &&
        ((resetRatio_ > 0 && minedRatio() >= resetRatio_) || live_.empty()))
        resetRemaining_ = resetDelay_;
}
void Mine::tick(double seconds) {
    if (options_["shaft"].isObject())
        return;
    if (!building_ && resetRemaining_ >= 0 && std::isfinite(seconds) && seconds >= 0) {
        resetRemaining_ = std::max(0.0, resetRemaining_ - seconds);
        if (resetRemaining_ <= 0)
            reset(rank_, prestige_);
    }
}
Hit Mine::raycast(Vec3 origin, Vec3 direction, float reach) const {
    Hit hit;
    const float length =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!std::isfinite(length) || length < 1e-6f || !std::isfinite(reach) || reach <= 0)
        return hit;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        std::abs(origin.x) > 10000 || std::abs(origin.y) > 10000 || std::abs(origin.z) > 10000)
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
Vec3 Mine::confinePlayer(Vec3 feet, float radius) const {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        std::abs(feet.x) > 10000 || std::abs(feet.y) > 10000 || std::abs(feet.z) > 10000)
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
    // the boundary also applies above the walls, including jumps from tree tops.
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
std::size_t Mine::enchantTargetCount(const std::string& id, int level, Cell origin) const {
    if (building_)
        return 0;
    if (id == "veinminer")
        return virtualMined_ >= live_.size() ? 0 : live_.size() - virtualMined_;
    std::size_t count = 0;
    if (id == "jackhammer") {
        for (int z = minimum_.z; z <= maximum_.z; ++z)
            for (int x = minimum_.x; x <= maximum_.x; ++x)
                if (code({x, origin.y, z}))
                    ++count;
        return count;
    }
    const int radius = level >= 2500 ? 2 : 1;
    for (int y = -radius; y <= radius; ++y)
        for (int z = -radius; z <= radius; ++z)
            for (int x = -radius; x <= radius; ++x) {
                if (x == 0 && y == 0 && z == 0)
                    continue;
                if (level < 250 && (y != 0 || std::abs(x) + std::abs(z) > 1))
                    continue;
                if (code({origin.x + x, origin.y + y, origin.z + z}))
                    ++count;
            }
    return count;
}
void Mine::applyVirtualMining(std::size_t count, std::uint64_t generation) {
    if (options_["shaft"].isObject())
        return;
    if (generation == generation_) {
        const auto limit = building_ ? cells_.size() : total_;
        virtualMined_ += std::min(count, limit - std::min(limit, virtualMined_));
        if (!building_)
            scheduleReset();
    }
}
bool Mine::queueBlast(Cell centre, int radius) {
    if (building_ || radius < 1 || radius > 20 || blasts_.size() >= 4)
        return false;
    if (centre.x < minimum_.x - 20 || centre.x > maximum_.x + 20 || centre.y < minimum_.y - 20 ||
        centre.y > maximum_.y + 20 || centre.z < minimum_.z - 20 || centre.z > maximum_.z + 20)
        return false;
    const Cell low{std::max(minimum_.x, centre.x - radius), std::max(minimum_.y, centre.y - radius),
                   std::max(minimum_.z, centre.z - radius)};
    const Cell high{std::min(maximum_.x, centre.x + radius), std::min(maximum_.y, centre.y + radius),
                    std::min(maximum_.z, centre.z + radius)};
    if (low.x > high.x || low.y > high.y || low.z > high.z)
        return false;
    blasts_.push_back({centre, low, high, low, radius, generation_});
    return true;
}
std::vector<MinedCell> Mine::advanceBlasts(std::size_t budget) {
    std::vector<MinedCell> removed;
    if (building_)
        return removed;
    budget = std::min<std::size_t>(budget, 512);
    removed.reserve(budget);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(1500);
    while (!blasts_.empty() && budget-- && std::chrono::steady_clock::now() < deadline) {
        auto& job = blasts_.front();
        if (job.generation != generation_) {
            blasts_.pop_front();
            continue;
        }
        const auto c = job.cursor;
        const int x = c.x - job.centre.x, y = c.y - job.centre.y, z = c.z - job.centre.z;
        if (x * x + y * y + z * z <= job.radius * job.radius) {
            auto block = dig(c);
            if (!block.empty())
                removed.push_back({c, std::move(block)});
        }
        if (++job.cursor.x > job.high.x) {
            job.cursor.x = job.low.x;
            if (++job.cursor.z > job.high.z) {
                job.cursor.z = job.low.z;
                ++job.cursor.y;
            }
        }
        if (job.cursor.y > job.high.y)
            blasts_.pop_front();
    }
    return removed;
}
Json Mine::snapshot() const {
    if (building_) {
        if (restoring_)
            return restore_;
        Json pending;
        pending["version"] = 1;
        pending["refill"] = true;
        pending["seed"] = static_cast<::Json::UInt64>(seed_);
        pending["options"] = options_;
        pending["pending_pocket_drops"] = pendingPocketDrops_;
        return pending;
    }
    Json out;
    out["version"] = 1;
    out["size"].append(width_);
    out["size"].append(height_);
    out["size"].append(depth_);
    out["minimum"].append(minimum_.x);
    out["minimum"].append(minimum_.y);
    out["minimum"].append(minimum_.z);
    out["rank_index"] = rank_;
    out["total_prestige"] = static_cast<::Json::Int64>(prestige_);
    std::array<char, 256> encoded{};
    encoded[0] = '0';
    unsigned next = 1;
    // Include a valid palette entry for a completely exhausted mine.
    for (auto c : cells_)
        if (c && encoded[c] == 0) {
            encoded[c] = Codes[next++];
            out["palette"].append(data_.mine["blocks"][c - 1]["id"]);
        }
    if (next == 1)
        out["palette"].append(data_.mine["blocks"][smooth_ - 1]["id"]);
    std::string cells;
    cells.resize(cells_.size());
    for (std::size_t i = 0; i < cells.size(); ++i)
        cells[i] = encoded[cells_[i]];
    out["cells"] = std::move(cells);
    out["total_blocks"] = static_cast<::Json::UInt64>(total_);
    out["virtual_mined"] = static_cast<::Json::UInt64>(virtualMined_);
    out["reset_remaining"] = resetRemaining_;
    out["seed"] = static_cast<::Json::UInt64>(seed_);
    out["options"] = options_;
    out["pocket_cells"] = Json(::Json::arrayValue);
    for (std::size_t i = 0; i < pocketCells_.size(); ++i)
        if (pocketCells_[i])
            out["pocket_cells"].append(static_cast<::Json::UInt64>(i));
    out["pending_pocket_drops"] = pendingPocketDrops_;
    return out;
}
bool Mine::loadSnapshot(const Json& source, int rank, std::int64_t prestige) {
    if (!source.isObject() || !integer(source["version"], 1, 1))
        return false;
    int pending = 0;
    if (source.isMember("pending_pocket_drops")) {
        if (!integer(source["pending_pocket_drops"], 0, 1000000000))
            return false;
        pending = source["pending_pocket_drops"].asInt();
    } else if (source.isMember("pending_relics")) {
        // retain undelivered ore rewards from saves made before collectible skins were removed.
        if (!source["pending_relics"].isObject() || source["pending_relics"].size() > 3)
            return false;
        for (const auto& amount : source["pending_relics"]) {
            if (!integer(amount, 0, 1000000000))
                return false;
            pending = std::min(1000000000, pending + amount.asInt());
        }
    }
    if (source["refill"].isBool() && source["refill"].asBool()) {
        const Json desired = nextOptions_;
        configure(source["options"]);
        reset(rank, prestige, source["seed"].isUInt64() ? source["seed"].asUInt64() : 0);
        nextOptions_ = desired.isNull() ? options_ : desired;
        pendingPocketDrops_ = pending;
        return true;
    }
    const auto& size = source["size"];
    if (!size.isArray() || size.size() != 3 || !integer(size[0], 2, 100) || !integer(size[1], 2, 50) ||
        !integer(size[2], 2, 100))
        return false;
    const auto& minimum = source["minimum"];
    if (!minimum.isNull() && (!minimum.isArray() || minimum.size() != 3))
        return false;
    MineBounds bounds{};
    bool matched = false;
    for (int p = 0; p <= MineSizePrestigeCap; ++p) {
        const auto candidate = mineBounds(data_, p);
        if (size[0].asInt() != candidate.maximum.x - candidate.minimum.x + 1 ||
            size[1].asInt() != candidate.maximum.y - candidate.minimum.y + 1 ||
            size[2].asInt() != candidate.maximum.z - candidate.minimum.z + 1)
            continue;
        if (!minimum.isNull() && (!integer(minimum[0], candidate.minimum.x, candidate.minimum.x) ||
                                  !integer(minimum[1], candidate.minimum.y, candidate.minimum.y) ||
                                  !integer(minimum[2], candidate.minimum.z, candidate.minimum.z)))
            continue;
        bounds = candidate;
        matched = true;
        break;
    }
    if (!matched)
        return false;
    const auto cellCount = std::size_t(size[0].asInt()) * size[1].asInt() * size[2].asInt();
    if (!integer(source["rank_index"], 0, 25) || source["rank_index"].asInt() != rank ||
        !integer(source["total_prestige"], 0, 9e15) || source["total_prestige"].asInt64() != prestige)
        return false;
    const auto& palette = source["palette"];
    if (!palette.isArray() || palette.empty() || palette.size() > 61 || !source["cells"].isString())
        return false;
    std::vector<std::uint8_t> mapped;
    std::set<std::string> seen;
    for (const auto& id : palette) {
        if (!id.isString() || id.asString().size() > 64 || !seen.insert(id.asString()).second)
            return false;
        unsigned found = 0;
        for (unsigned i = 0; i < data_.mine["blocks"].size() && i < 61; ++i)
            if (data_.mine["blocks"][i]["id"] == id) {
                found = i + 1;
                break;
            }
        if (found == 0)
            return false;
        mapped.push_back(static_cast<std::uint8_t>(found));
    }
    const auto cells = source["cells"].asString();
    if (cells.size() != cellCount)
        return false;
    std::size_t remaining = 0;
    for (char c : cells) {
        const auto n = std::string_view(Codes).find(c);
        if (n == std::string_view::npos || n > palette.size())
            return false;
        if (n)
            ++remaining;
    }
    if (!integer(source["total_blocks"], std::max<std::size_t>(1, remaining), cellCount) ||
        !integer(source.get("virtual_mined", 0), 0, source["total_blocks"].asDouble()))
        return false;
    const auto countdown = source.get("reset_remaining", -1);
    if (!countdown.isNumeric() || !std::isfinite(countdown.asDouble()) || countdown.asDouble() < -1 ||
        countdown.asDouble() > 30)
        return false;
    std::vector<std::uint8_t> pockets(cellCount);
    Json savedPockets(::Json::arrayValue);
    const auto& oldPockets = source.isMember("pocket_cells") ? source["pocket_cells"] : source["relic_cells"];
    if (!oldPockets.isNull()) {
        if (!oldPockets.isArray() || oldPockets.size() > 7)
            return false;
        for (const auto& row : oldPockets) {
            const auto& value = row.isObject() ? row["index"] : row;
            if (!integer(value, 0, cellCount - 1))
                return false;
            const auto at = value.asUInt64();
            if (pockets[at] || cells[at] == '0')
                return false;
            pockets[at] = 1;
            savedPockets.append(value);
        }
    }
    const auto seed = source["seed"].isUInt64() ? source["seed"].asUInt64() : 1;
    const Json desired = nextOptions_;
    configure(source["options"]);
    beginReset(rank, prestige, seed ? seed : 1, bounds);
    nextOptions_ = desired.isNull() ? options_ : desired;
    restoring_ = true;
    restore_ = source;
    restore_.removeMember("cave");
    restore_.removeMember("crate");
    restore_.removeMember("relic_cells");
    restore_.removeMember("pending_relics");
    restore_["pocket_cells"] = std::move(savedPockets);
    restore_["pending_pocket_drops"] = pending;
    restorePalette_ = std::move(mapped);
    pocketCells_ = std::move(pockets);
    pendingPocketDrops_ = pending;
    return true;
}
} // namespace prison
