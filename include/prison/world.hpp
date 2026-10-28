#pragma once
#include "common.hpp"
#include <deque>
#include <limits>

namespace prison {

// mine blocks occupy the first 52 tiles; the remaining tiles are scenery.
constexpr int TerrainAtlasColumns = 8;
constexpr int TerrainAtlasRows = 16;
constexpr int TerrainBedrockTile = 52;
constexpr int TerrainPocketTile = 53;
constexpr int TerrainMasonryTile = 54;
constexpr int TerrainPavingTile = 55;
constexpr int TerrainTimberTile = 56;
constexpr int TerrainMetalTile = 57;
constexpr int TerrainLampTile = 58;
constexpr int TerrainTrimTile = 59;
constexpr int TerrainGrassTile = 60;
constexpr int TerrainLeavesTile = 61;
constexpr int TerrainRockTile = 62;
constexpr int TerrainDirtTile = 63;
constexpr int TerrainTimberEndTile = 64;
constexpr int TerrainGrassSideTile = 65;
constexpr int TerrainTreeTile = 66;
constexpr int TerrainTreeEndTile = 67;
constexpr int TerrainTallGrassTile = 68;
constexpr int TerrainTallGrassVariants = 3;
constexpr int TerrainFlowerTile = 71;
constexpr int TerrainFlowerVariants = 3;
struct Vertex {
    float x, y, z, u, v, r, g, b;
};
struct ChunkMesh {
    std::vector<Vertex> vertices;
    std::uint64_t revision = 0;
    Vec3 minimum, maximum;
};
struct Hit {
    bool hit = false, mineable = false;
    Cell cell;
    Vec3 point, normal;
    float distance = 0;
    std::string block;
};
struct MoveResult {
    Vec3 position;
    bool grounded = false;
};
struct MinedCell {
    Cell cell;
    std::string block;
};

constexpr int MineSizePrestigeCap = 50;
struct MineBounds {
    Cell minimum, maximum;
};
MineBounds mineBounds(const Data& data, std::int64_t totalPrestige);

class Mine {
  public:
    explicit Mine(const Data& data);
    void configure(const Json& options);
    int upgradeLevel(const std::string& id) const;
    void reset(int rank, std::int64_t totalPrestige = 0, std::uint64_t seed = 0);
    // Work is bounded by counts as well as a 3 ms clock budget. Returns true
    // only on the frame in which a refill finishes.
    bool step(std::size_t cellBudget = 8192, std::size_t chunkBudget = 64);
    void tick(double seconds);
    bool resetting() const {
        return building_;
    }
    std::uint64_t generation() const {
        return generation_;
    }
    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    int depth() const {
        return depth_;
    }
    Cell minimum() const {
        return minimum_;
    }
    Cell maximum() const {
        return maximum_;
    }
    Vec3 spawn() const {
        return spawn_;
    }
    // Camera yaw zero faces -Z, toward the centre of the mine from spawn.
    float spawnYaw() const {
        return 0;
    }
    std::size_t remaining() const {
        return live_.size();
    }
    std::size_t total() const {
        return total_;
    }
    double minedRatio() const;
    double resetRemaining() const {
        return resetRemaining_;
    }
    const std::vector<ChunkMesh>& chunks() const {
        return chunks_;
    }
    const ChunkMesh& shellMesh() const {
        return shellMesh_;
    }
    std::size_t pendingChunks() const {
        return dirty_.size();
    }
    std::string blockId(Cell cell) const;
    Json rewardSample() const;
    int takePocketDrops(int limit = 32);
    const std::string& themeName() const {
        return themeName_;
    }
    Color skyColor() const {
        return sky_;
    }
    const Json& definition(Cell cell) const;
    bool solid(Cell cell) const;
    bool solidAt(Vec3 point) const;
    void setGuardian(const Json& guardian);
    bool inside(Cell cell) const;
    std::string dig(Cell cell);
    bool nextTarget(Cell& cell) const;
    Hit raycast(Vec3 origin, Vec3 direction, float reach) const;
    Vec3 confinePlayer(Vec3 feet, float radius = .30f) const;
    bool spaceFree(Vec3 feet, float radius, float height) const;
    MoveResult movePlayer(Vec3 feet, Vec3 displacement, float radius = .30f, float height = 1.80f) const;
    std::size_t enchantTargetCount(const std::string& id, int level, Cell origin) const;
    void applyVirtualMining(std::size_t count, std::uint64_t generation);
    bool queueBlast(Cell centre, int radius);
    std::vector<MinedCell> advanceBlasts(std::size_t scanBudget = 128);
    std::size_t pendingBlasts() const {
        return blasts_.size();
    }
    Json snapshot() const;
    bool loadSnapshot(const Json& snapshot, int rank, std::int64_t totalPrestige = 0);

  private:
    struct Box {
        Vec3 low, high;
        int tile = TerrainBedrockTile;
    };
    struct Plant {
        Vec3 position;
        float height;
        int tile;
    };
    struct Blast {
        Cell centre, low, high, cursor;
        int radius;
        std::uint64_t generation;
    };
    const Data& data_;
    Cell minimum_{-12, -7, -12}, maximum_{12, -1, 12};
    int width_ = 25, height_ = 7, depth_ = 25, chunkX_ = 0, chunkY_ = 0, chunkZ_ = 0;
    Vec3 spawn_{0, 2, 18};
    Vec3 walkMinimum_, walkMaximum_;
    int rank_ = 0, smooth_ = 1, ore_ = 2;
    std::int64_t prestige_ = 0;
    std::uint64_t seed_ = 0, generation_ = 0;
    Json options_, nextOptions_;
    std::string layout_ = "scattered";
    float oreRatio_ = .06f;
    std::vector<std::uint8_t> cells_;
    std::vector<std::uint8_t> pocketCells_;
    int pendingPocketDrops_ = 0;
    std::string themeName_;
    Color sky_{.055f, .066f, .09f, 1};
    std::vector<std::uint32_t> live_, livePositions_;
    std::vector<ChunkMesh> chunks_;
    ChunkMesh shellMesh_;
    std::vector<Box> shell_;
    std::vector<Plant> plants_;
    std::vector<Box> guardianBoxes_;
    std::deque<std::size_t> dirty_;
    std::vector<bool> dirtyFlags_;
    std::deque<Blast> blasts_;
    std::size_t cursor_ = 0, total_ = 0, virtualMined_ = 0;
    bool building_ = false, restoring_ = false;
    Json restore_;
    std::vector<std::uint8_t> restorePalette_;
    double resetRemaining_ = -1, resetDelay_ = 10, resetRatio_ = .3;
    std::size_t index(Cell cell) const;
    Cell position(std::size_t index) const;
    std::uint8_t code(Cell cell) const;
    void markDirty(Cell cell);
    void markAllDirty();
    void buildChunk(std::size_t index);
    void buildShell();
    void buildScenery();
    void buildSurroundings(float left, float right, float back, float front);
    void generateDiscoveries();
    void scheduleReset();
    bool collisionBox(const Box& box) const;
    bool resize(const MineBounds& bounds);
    void beginReset(int rank, std::int64_t prestige, std::uint64_t seed, const MineBounds& bounds);
};
} // namespace prison
