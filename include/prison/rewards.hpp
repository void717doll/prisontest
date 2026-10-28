#pragma once
#include "game.hpp"
#include "world.hpp"
#include <deque>

namespace prison {
class RewardQueue {
  public:
    bool enqueue(Game& game, Mine& mine, const Proc& proc, Cell origin, const Json& context = Json());
    std::size_t step(Game& game, Mine& mine, std::size_t budget = 512, double milliseconds = 2);
    Json snapshot(const Mine& mine) const;
    bool restore(const Json& value, Mine& mine);
    void clear() {
        jobs_.clear();
    }
    std::size_t size() const {
        return jobs_.size();
    }
    bool empty() const {
        return jobs_.empty();
    }

  private:
    struct Job {
        std::string source;
        int total = 0, remaining = 0, compressed = 0;
        int terrainTotal = 0, terrainCursor = 0;
        bool visual = false, rewardsFinished = false, cross = false;
        bool trackPayout = true;
        double paidMoney = 0, paidTokens = 0;
        std::uint64_t generation = 0;
        Json context, sample;
        Cell low{}, high{}, origin{};
    };
    std::deque<Job> jobs_;
};
} // namespace prison
