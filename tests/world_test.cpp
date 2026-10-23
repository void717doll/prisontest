#include "prison/world.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace prison;
namespace {
void check(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}
void finish(Mine& mine) {
    int steps = 0;
    while (mine.resetting()) {
        mine.step(128, 2);
        check(++steps < 10000, "refill failed to complete");
    }
    while (mine.pendingChunks())
        mine.step(0, 2);
}
Data load(const std::filesystem::path& root) {
    Data data;
    std::ifstream mine(root / "data/mine.json"), balance(root / "data/balance.json");
    check(static_cast<bool>(mine) && static_cast<bool>(balance), "cannot read source data");
    mine >> data.mine;
    balance >> data.balance;
    return data;
}
} // namespace
int main(int argc, char** argv) {
    try {
#ifdef PRISON_SOURCE_DIR
        const std::filesystem::path root = argc > 1 ? argv[1] : PRISON_SOURCE_DIR;
#else
        const std::filesystem::path root = argc > 1 ? argv[1] : ".";
#endif
        const auto data = load(root);
        Mine mine(data);
        mine.reset(0, 0, 1234);
        check(mine.resetting(), "reset must gate gameplay");
        check(mine.dig({0, -1, 0}).empty(), "mining allowed during refill");
        check(mine.snapshot()["refill"].asBool(), "incomplete refill persisted as terrain");
        mine.step(64, 1);
        check(mine.resetting(), "refill ignored work budget");
        const auto generation = mine.generation();
        mine.reset(0);
        check(mine.generation() == generation, "duplicate reset restarted generation");
        finish(mine);
        check(mine.remaining() > 0, "empty initial mine");
        check(mine.width() == 25 && mine.depth() == 25 && mine.height() == 7, "source dimensions changed");
        for (int x = -12; x <= 12; ++x)
            for (int z = -12; z <= 12; ++z)
                check(!mine.blockId({x, -1, z}).empty() && !mine.blockId({x, -2, z}).empty(),
                      "top two layers must remain solid");
        const auto original = mine.snapshot();
        Mine same(data);
        same.reset(0, 0, 1234);
        finish(same);
        check(original["cells"] == same.snapshot()["cells"], "seeded generation is not deterministic");
        const auto hit = mine.raycast({0, 4, 0}, {0, -1, 0}, 6);
        check(hit.hit && hit.mineable && hit.cell.y == -1 && std::abs(hit.distance - 4.5f) < .001f,
              "raycast targets wrong surface");
        check(!mine.raycast({0, 4, 0}, {0, -1, 0}, 4).hit, "raycast exceeds reach");
        check(!mine.raycast({0, 4, 0}, {0, 0, 0}, 6).hit, "zero direction ray is invalid");
        const auto before = mine.remaining();
        check(!mine.dig(hit.cell).empty(), "surface block cannot be dug");
        check(mine.remaining() + 1 == before && mine.dig(hit.cell).empty(), "dig duplicated a block");
        check(mine.raycast({0, 4, 0}, {0, -1, 0}, 7).cell.y == -2,
              "raycast did not advance to exposed neighbour");
        check(mine.pendingChunks() > 0, "dig did not dirty geometry");
        const auto unmeshed = mine.snapshot();
        check(mine.pendingChunks() > 0, "snapshot must not rebuild dirty meshes");
        finish(mine);
        const auto saved = mine.snapshot();
        check(unmeshed == saved, "snapshot depends on rendered mesh state");
        Mine restored(data);
        check(restored.loadSnapshot(saved, 0), "valid snapshot rejected");
        check(restored.snapshot() == saved, "pending import lost original snapshot");
        finish(restored);
        check(restored.snapshot()["cells"] == saved["cells"] && restored.remaining() == mine.remaining(),
              "excavations changed across load");
        check(!restored.loadSnapshot(saved, 1), "wrong-rank snapshot accepted");
        auto damaged = saved;
        std::string bad = damaged["cells"].asString();
        bad[0] = '!';
        damaged["cells"] = bad;
        check(!restored.loadSnapshot(damaged, 0) && !restored.resetting(),
              "invalid snapshot mutated live mine");
        damaged = saved;
        damaged["palette"].append(damaged["palette"][0]);
        check(!restored.loadSnapshot(damaged, 0), "duplicate palette accepted");

        Mine growing(data);
        growing.reset(0, 1, 1234);
        check(growing.width() == 26 && growing.depth() == 26, "first prestige does not expand the mine");
        growing.reset(0, MineSizePrestigeCap, 1234);
        check(growing.width() == 100 && growing.depth() == 100 && growing.height() == 49,
              "prestige 50 does not match the reference maximum");
        check(growing.spawn().z > growing.maximum().z + 3, "expanded mine swallowed the spawn pad");
        finish(growing);
        check(!growing.dig({0, -1, 0}).empty(), "expanded mine surface cannot be dug");
        const auto largeSaved = growing.snapshot();
        Mine largeRestored(data);
        check(largeRestored.loadSnapshot(largeSaved, 0, MineSizePrestigeCap),
              "expanded mine snapshot rejected");
        finish(largeRestored);
        check(largeRestored.width() == 100 && largeRestored.snapshot()["cells"] == largeSaved["cells"],
              "expanded excavation changed after reload");
        auto legacy = saved;
        legacy.removeMember("minimum");
        legacy["total_prestige"] = MineSizePrestigeCap;
        check(largeRestored.loadSnapshot(legacy, 0, MineSizePrestigeCap), "legacy small mine rejected");
        finish(largeRestored);
        check(largeRestored.width() == 25 && largeRestored.snapshot()["cells"] == saved["cells"],
              "legacy login resized or refilled existing excavations");
        largeRestored.reset(0, MineSizePrestigeCap);
        check(largeRestored.width() == 100, "legacy mine failed to grow on its next reset");

        mine.reset(0, 0, 1234);
        finish(mine);
        auto landed = mine.movePlayer({0, 5, 0}, {0, -100, 0});
        check(landed.grounded && std::abs(landed.position.y + .5f) < .001f,
              "swept vertical collision tunneled");
        auto stopped = mine.movePlayer(landed.position, {100, 0, 0});
        check(stopped.position.x <= 12.201f, "swept wall collision tunneled");
        auto pad = mine.movePlayer(mine.spawn(), {0, -10, 0});
        check(pad.grounded && std::abs(pad.position.y - 1.5f) < .001f, "spawn pad collision is wrong");
        check(mine.spawnYaw() == 0, "spawn must face toward mine");
        check(mine.enchantTargetCount("jackhammer", 1, {0, -1, 0}) == 625,
              "jackhammer layer target count incorrect");
        check(mine.enchantTargetCount("explosive", 1, {0, -1, 0}) == 4,
              "small explosive target count incorrect");
        check(mine.queueBlast({0, -1, 0}, 1), "valid blast rejected");
        const auto blastBefore = mine.remaining();
        std::size_t removed = 0;
        while (mine.pendingBlasts()) {
            auto batch = mine.advanceBlasts(2);
            check(batch.size() <= 2, "blast exceeds work budget");
            removed += batch.size();
        }
        check(removed == 6 && mine.remaining() + removed == blastBefore, "blast sphere removal is wrong");
        check(!mine.queueBlast({0, -1, 0}, 21), "invalid bomb tier accepted");
        const double oldRatio = mine.minedRatio();
        mine.applyVirtualMining(1000, mine.generation() - 1);
        check(mine.minedRatio() == oldRatio, "stale proc depleted new generation");
        mine.applyVirtualMining(mine.total() / 2, mine.generation());
        check(mine.resetRemaining() > 0, "depletion did not schedule reset");
        mine.tick(31);
        check(mine.resetting(), "automatic reset never fired");
        finish(mine);
        const auto start = std::chrono::steady_clock::now();
        std::size_t mined = 0;
        for (int pass = 0; pass < 8; ++pass) {
            mine.reset(pass % 26, 0, 2000 + pass);
            finish(mine);
            Cell c;
            while (mined < static_cast<std::size_t>((pass + 1) * 2000) && mine.nextTarget(c)) {
                mine.dig(c);
                ++mined;
                if (mined % 128 == 0)
                    mine.step(0, 2);
            }
            finish(mine);
        }
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "world: deterministic reset, persisted excavation, bounded meshes/blasts, raycast, "
                     "swept collision passed; "
                  << mined << " digs and 8 resets in " << ms << " ms\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "world test: " << e.what() << '\n';
        return 1;
    }
}
