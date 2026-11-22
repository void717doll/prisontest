#include "prison/game.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
constexpr std::time_t day = 20000 * 86400;
constexpr std::time_t tomorrow = day + 86400;

void claims(const prison::Data& data) {
    prison::Game game(data);
    const auto first = game.dailyReward(day);
    check(first["points"] == 5 && first["money"] == 1e6 && first["tokens"] == 1e6 &&
              first["crystal_tier"] == 1 && first["streak"] == 1,
          "first claim offers base currencies and a T1 crystal");
    check(game.claimDailyReward(tomorrow - 1).ok, "claim before utc midnight");
    const auto claimed = game.snapshot();
    check(!game.claimDailyReward(tomorrow - 1).ok && game.snapshot() == claimed,
          "repeated claims do not pay or change the streak");
    game.loadSnapshot(claimed);
    check(!game.claimDailyReward(day).ok && !game.claimDailyReward(day - 86400).ok,
          "reload and a backwards clock cannot repeat a claim");
    check(game.claimDailyReward(tomorrow).ok && game.state["daily_rewards"]["streak"] == 2 &&
              game.dailyReward(tomorrow)["money"] == 1100000.0,
          "utc midnight advances the streak and reward");
    check(game.claimDailyReward(tomorrow + 2 * 86400).ok && game.state["daily_rewards"]["streak"] == 1 &&
              game.state["daily_rewards"]["best_streak"] == 2,
          "missing one day resets the streak and keeps the record");
    check(game.statistic("daily_rewards_claimed") == 3 && game.statistic("money_earned") == 3100000,
          "daily claims and income are counted once");
    prison::Game other(data);
    check(other.dailyReward(day)["available"].asBool(), "worlds have separate claims");
    check(!other.claimDailyReward(-1).ok, "failed clock read cannot award a claim");
    check(other.command("/daily").menu == "rewards" && !other.command("/daily unknown").ok,
          "daily command opens rewards and validates its arguments");
    check(other.command("/daily claim").ok && !other.command("/daily claim").ok,
          "command claims use the same duplicate protection");
}

void scaling(const prison::Data& data) {
    prison::Game game(data);
    for (int i = 0; i < 35; ++i)
        check(game.claimDailyReward(day + i * 86400).ok, "consecutive daily claim");
    const auto reward = game.dailyReward(day + 34 * 86400);
    check(reward["streak"] == 35 && reward["money"] == 3900000.0 && reward["crystal_tier"] == 3,
          "streak counts past thirty while rewards stop growing");
    double previous = 0;
    for (const auto& milestone : data.systems["daily_rewards"]["milestones"]) {
        game.state["total_prestige"] = milestone["prestige"];
        const auto next = game.dailyReward(day + 36 * 86400);
        check(next["money"] == milestone["money"] && next["money"].asDouble() > previous,
              "prestige thresholds increase the base reward after a missed day");
        previous = next["money"].asDouble();
    }
    game.state["total_prestige"] = 101;
    game.state["prestige_level"] = 0;
    game.state["rebirth"] = 1;
    check(game.dailyReward(day + 36 * 86400)["money"] == previous * 1.5,
          "rebirth keeps the total-prestige tier and increases rewards");
    game.state["rebirth"] = 200;
    const auto capped = game.dailyReward(day + 35 * 86400);
    check(capped["crystal_tier"] == 10 && capped["rebirth_multiplier"] == 51.0,
          "crystal and rebirth bonuses stay bounded");
}

void storage(const prison::Data& data) {
    prison::Game game(data);
    for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i) {
        auto& item = game.state["inventory_slots"][i];
        item["id"] = "pet";
        item["count"] = 1;
    }
    const auto before = game.snapshot();
    check(!game.claimDailyReward(day).ok && game.snapshot() == before,
          "full inventory leaves the entire claim untouched");
    game.state["inventory_slots"][4] = prison::Json();
    game.state["cleaner_settings"]["enabled"] = true;
    game.state["cleaner_settings"]["mode"] = "blacklist";
    game.state["cleaner_settings"]["default_action"] = "salvage";
    check(game.claimDailyReward(day).ok && game.state["inventory_slots"][4]["id"] == "crystal",
          "daily crystal bypasses automatic filters");
    check(game.claimDailyReward(tomorrow).ok && game.state["inventory_slots"][4]["count"] == 2,
          "matching daily crystals stack even when every slot is occupied");
    game.newWorld();
    game.state["money"] = data.balance["currency_cap"];
    game.state["tokens"] = data.balance["currency_cap"];
    game.state["points"] = 9e15 - 2;
    check(game.claimDailyReward(day).ok && game.state["points"] == 9e15 &&
              game.statistic("points_earned") == 2 && game.statistic("money_earned") == 0,
          "capped wallets record only actual income");
    auto saved = game.snapshot();
    saved["daily_rewards"] = "bad";
    game.loadSnapshot(saved);
    check(game.dailyReward(day)["streak"] == 1 && game.dailyReward(day)["available"].asBool(),
          "malformed daily metadata is normalized");
    saved.removeMember("daily_rewards");
    game.loadSnapshot(saved);
    check(game.dailyReward(day)["available"].asBool(), "older worlds can claim their first reward");
}
} // namespace

int main(int argc, char** argv) {
    try {
        prison::Data data;
        data.load(argc > 1 ? argv[1] : PRISON_SOURCE_DIR);
        claims(data);
        scaling(data);
        storage(data);
        std::cout << "daily rewards, streaks and persistence checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "daily_test: " << error.what() << '\n';
        return 1;
    }
}
