#include "prison/rewards.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void finish(prison::Mine& mine) {
    int steps = 0;
    while (mine.resetting() || mine.pendingChunks()) {
        mine.step(512, 2);
        check(++steps < 10000, "mine refill did not finish");
    }
}

void drain(prison::RewardQueue& rewards, prison::Game& game, prison::Mine& mine) {
    int steps = 0;
    while (!rewards.empty()) {
        rewards.step(game, mine, 512, 10);
        mine.step(512, 2);
        check(++steps < 10000, "reward queue did not finish");
    }
}

void savedQueue(const prison::Data& data) {
    prison::Game game(data);
    game.random.seed(445566);
    prison::Mine mine(data);
    mine.reset(0, 0, 12345);
    finish(mine);
    prison::RewardQueue rewards;
    check(rewards.enqueue(game, mine, {"jackhammer", 1}, {0, -1, 0}), "jackhammer enqueue failed");
    check(rewards.step(game, mine, 7, 10) == 7, "reward queue ignored sample budget");
    check(game.state["blocks_mined"].asInt() == 7, "partial queue block count is wrong");
    game.state["native_reward_jobs"] = rewards.snapshot(mine);
    game.state["mine_state"] = mine.snapshot();
    const auto saved = game.snapshot();
    prison::Game restoredGame(data);
    restoredGame.loadSnapshot(saved);
    restoredGame.random = game.random;
    prison::Mine restoredMine(data);
    check(restoredMine.loadSnapshot(restoredGame.state["mine_state"], 0), "saved mine rejected");
    prison::RewardQueue restoredRewards;
    check(restoredRewards.restore(restoredGame.state["native_reward_jobs"], restoredMine),
          "saved reward queue rejected");
    check(restoredRewards.snapshot(restoredMine) == rewards.snapshot(mine), "queue changed across restore");

    drain(rewards, game, mine);
    drain(restoredRewards, restoredGame, restoredMine);
    for (const char* key :
         {"money", "tokens", "xp", "pick_xp", "pick_level", "blocks_mined", "points", "reward_claims"})
        check(game.state[key] == restoredGame.state[key], "reloaded queue changed final accounting");
    check(game.state["blocks_mined"].asInt() == 625, "saved queue lost or duplicated pending blocks");
    check(game.state["pick_xp"].asDouble() == 0, "jackhammer awarded pickaxe experience");
    check(mine.snapshot()["virtual_mined"] == restoredMine.snapshot()["virtual_mined"],
          "saved queue changed compressed depletion");
    check(mine.snapshot()["virtual_mined"].asInt() == 31, "jackhammer compressed count changed");
}

void resetsAndCapturedContext(const prison::Data& data) {
    prison::Game game(data);
    prison::Mine mine(data);
    mine.reset(0, 0, 12345);
    finish(mine);
    prison::RewardQueue rewards;
    check(rewards.enqueue(game, mine, {"explosive", 1}, {0, -1, 0}), "explosive enqueue failed");
    auto original = rewards.snapshot(mine);
    original["jobs"][0]["sample"]["ore_ratio"] = 0;
    check(rewards.restore(original, mine), "deterministic reward sample rejected");
    game.state["enchants"]["fortune"] = 10000;
    game.state["enchants"]["experienced"] = 1000;
    game.state["pick_level"] = 4000;
    mine.reset(1, 0, 54321);
    finish(mine);
    check(!rewards.snapshot(mine)["jobs"][0]["terrain_pending"].asBool(),
          "reset did not stale terrain mutation");
    const auto terrain = mine.snapshot();
    drain(rewards, game, mine);
    check(game.state["blocks_mined"].asInt() == 4 && game.state["money"].asDouble() == 380,
          "reset lost rewards or used new equipment for old rewards");
    check(std::abs(game.state["pick_xp"].asDouble() - 4 * .8143) < 1e-9,
          "captured explosive experience changed after upgrade");
    check(mine.snapshot()["virtual_mined"] == terrain["virtual_mined"] &&
              mine.snapshot()["cells"] == terrain["cells"],
          "stale reward job changed a new mine generation");

    game.newWorld();
    mine.reset(0, 0, 12345);
    finish(mine);
    check(rewards.enqueue(game, mine, {"veinminer", 1}, {0, -1, 0}), "veinminer enqueue failed");
    const auto row = rewards.snapshot(mine)["jobs"][0];
    const auto expected = mine.remaining() * .20;
    check(row["total"].asDouble() >= std::floor(expected) && row["total"].asDouble() <= std::ceil(expected),
          "veinminer ignored source twenty percent target ratio");
    check(row["total"].asInt() <= 5000, "veinminer exceeded reward cap");
    const auto before = game.snapshot();
    check(rewards.step(game, mine, 0) == 0 && game.snapshot() == before, "zero work budget paid rewards");
    check(rewards.step(game, mine, 512, std::numeric_limits<double>::quiet_NaN()) == 0 &&
              game.snapshot() == before,
          "invalid time budget paid rewards");
}

void invalidSnapshots(const prison::Data& data) {
    prison::Game game(data);
    prison::Mine mine(data);
    mine.reset(0, 0, 12345);
    finish(mine);
    prison::RewardQueue rewards;
    check(rewards.enqueue(game, mine, {"jackhammer", 1}, {0, -1, 0}), "fixture enqueue failed");
    const auto original = rewards.snapshot(mine);
    const auto state = game.snapshot();
    std::vector<prison::Json> invalid;
    auto row = original;
    row["jobs"][0]["total"] = 5001;
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["remaining"] = 626;
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["compressed"] = 250;
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["source"] = "manual";
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["sample"]["smooth"] = "../unexpected";
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["sample"]["ore_ratio"] = 2;
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["context"]["money_mult"] = std::numeric_limits<double>::infinity();
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["context"]["enchants"]["fortune"] = "10000";
    invalid.push_back(row);
    row = original;
    row["jobs"][0]["terrain_pending"] = "yes";
    invalid.push_back(row);
    row = original;
    for (int i = 1; i < 65; ++i)
        row["jobs"].append(original["jobs"][0]);
    invalid.push_back(row);
    for (const auto& value : invalid) {
        check(!rewards.restore(value, mine), "malformed reward snapshot accepted");
        check(rewards.snapshot(mine) == original && game.snapshot() == state,
              "failed queue restore mutated state");
    }
    rewards.clear();
    for (int i = 0; i < 64; ++i)
        check(rewards.enqueue(game, mine, {"explosive", 1}, {0, -1, 0}), "queue filled before its limit");
    check(rewards.size() == 64 && !rewards.enqueue(game, mine, {"explosive", 1}, {0, -1, 0}),
          "reward queue exceeded size limit");
    check(rewards.restore(prison::Json(), mine) && rewards.empty(),
          "old save without queue did not restore empty");

    auto completed = original;
    completed["jobs"][0]["remaining"] = 0;
    mine.reset(0, 0, 998877);
    check(rewards.restore(completed, mine), "completed reward awaiting terrain could not restore");
    const auto blocks = game.state["blocks_mined"];
    rewards.step(game, mine);
    check(!rewards.empty() && game.state["blocks_mined"] == blocks,
          "pending terrain completion repaid blocks");
    finish(mine);
    rewards.step(game, mine);
    check(rewards.empty() && mine.snapshot()["virtual_mined"].asInt() == 31 &&
              game.state["blocks_mined"] == blocks,
          "restored refill lost or duplicated compressed reward completion");
}

void drill(const prison::Data& data) {
    prison::Game game(data);
    game.state["message_settings"]["visual_enchants"] = true;
    prison::Mine mine(data);
    mine.reset(0, 0, 12345);
    finish(mine);
    prison::RewardQueue queue;
    const auto low = mine.minimum(), high = mine.maximum();
    const prison::Cell origin{0, high.y, 0};
    const auto live = mine.remaining();
    check(queue.enqueue(game, mine, {"drill", 1500}, origin), "drill queues real terrain work");
    check(queue.step(game, mine, 3, 10) <= 3, "drill respects scan budget");
    const auto savedQueue = queue.snapshot(mine);
    const auto savedMine = mine.snapshot();
    prison::Game resumed(data);
    resumed.loadSnapshot(game.snapshot());
    prison::Mine restored(data);
    check(restored.loadSnapshot(savedMine, 0), "drilled mine restores");
    prison::RewardQueue pending;
    check(pending.restore(savedQueue, restored), "drill cursor restores");
    drain(pending, resumed, restored);
    for (int y = low.y; y <= high.y; ++y)
        for (int z = -2; z <= 2; ++z)
            for (int x = -2; x <= 2; ++x)
                check(!restored.solid({x, y, z}), "drill reaches the bottom across its width");
    check(resumed.state["blocks_mined"].asUInt64() == live - restored.remaining(),
          "drill only rewards actual removals, including across saves");
    check(queue.enqueue(game, mine, {"drill", 1}, origin), "second drill enqueued");
    mine.reset(0, 0, 555);
    finish(mine);
    const auto resetCount = mine.remaining();
    drain(queue, game, mine);
    check(mine.remaining() == resetCount, "old drills cannot carve a newly reset mine");
}
void visualModes(const prison::Data& data) {
    for (const std::string source : {"explosive", "jackhammer", "veinminer", "drill"}) {
        prison::Game quiet(data), visible(data);
        quiet.random.seed(112233);
        visible.random = quiet.random;
        visible.state["message_settings"]["visual_enchants"] = true;
        prison::Mine quietMine(data), visibleMine(data);
        quietMine.reset(0, 0, 12345);
        visibleMine.reset(0, 0, 12345);
        finish(quietMine);
        finish(visibleMine);
        const auto original = quietMine.snapshot()["cells"];
        const prison::Cell origin{0, -1, 0};
        prison::RewardQueue quietQueue, visibleQueue;
        check(quietQueue.enqueue(quiet, quietMine, {source, 1500}, origin), "nonvisual mode enqueues");
        check(visibleQueue.enqueue(visible, visibleMine, {source, 1500}, origin), "visual mode enqueues");
        visibleQueue.step(visible, visibleMine, 512, 10);
        const auto queueState = visibleQueue.snapshot(visibleMine);
        prison::Game resumed(data);
        resumed.loadSnapshot(visible.snapshot());
        resumed.random = visible.random;
        resumed.state["message_settings"]["visual_enchants"] = false;
        prison::Mine resumedMine(data);
        check(resumedMine.loadSnapshot(visibleMine.snapshot(), 0), "visual mine resumes");
        prison::RewardQueue resumedQueue;
        check(resumedQueue.restore(queueState, resumedMine), "visual cursor resumes");
        drain(quietQueue, quiet, quietMine);
        drain(visibleQueue, visible, visibleMine);
        drain(resumedQueue, resumed, resumedMine);
        check(quietMine.snapshot()["cells"] == original, "nonvisual enchants must leave terrain unchanged");
        check(visibleMine.snapshot()["cells"] != original, "visual enchants must remove terrain");
        check(visibleMine.snapshot()["cells"] == resumedMine.snapshot()["cells"],
              "saved jobs retain their visual mode when the setting changes");
        for (const char* key : {"money", "tokens", "pick_xp", "blocks_mined", "points"}) {
            check(quiet.state[key] == visible.state[key], "visual choice must not multiply proc payouts");
            check(resumed.state[key] == visible.state[key], "resumed visual work must not repay rewards");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        prison::Data data;
        data.load(argc > 1 ? argv[1] : "..");
        savedQueue(data);
        resetsAndCapturedContext(data);
        invalidSnapshots(data);
        drill(data);
        visualModes(data);
        std::cout << "rewards: bounded work, captured context, save continuity, reset isolation and invalid "
                     "queues passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "reward queue test: " << error.what() << '\n';
        return 1;
    }
}
