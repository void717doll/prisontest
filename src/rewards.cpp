#include "prison/rewards.hpp"
#include <chrono>

namespace prison {
namespace {
constexpr std::size_t maximumJobs = 64;
constexpr int maximumBlocks = 5000;

bool validNumber(const Json& value, double low, double high, bool integer = false) {
    if (!value.isNumeric())
        return false;
    const double number = value.asDouble();
    return std::isfinite(number) && number >= low && number <= high &&
           (!integer || std::floor(number) == number);
}

bool validSource(const std::string& source) {
    return source == "explosive" || source == "jackhammer" || source == "veinminer" || source == "drill";
}

bool validSample(const Json& sample) {
    if (!sample.isObject() || !sample["smooth"].isString() || !sample["ore"].isString() ||
        !validNumber(sample["ore_ratio"], 0, 1))
        return false;
    const auto smooth = sample["smooth"].asString();
    if (smooth.size() != 13 || smooth.compare(0, 5, "tier_") != 0 || smooth[5] < 'a' || smooth[5] > 'z' ||
        smooth.compare(6, 7, "_smooth") != 0)
        return false;
    const auto ore = sample["ore"].asString();
    return ore.size() == 10 && ore.compare(0, 5, "tier_") == 0 && ore[5] >= 'a' && ore[5] <= 'z' &&
           ore.compare(6, 4, "_ore") == 0;
}

bool validContext(const Json& context) {
    for (const char* key : {"surge_mult", "overdrive_mult"})
        if (context.isObject() && context.isMember(key) &&
            !validNumber(context[key], 1, std::string(key) == "surge_mult" ? 1.5 : 2))
            return false;
    if (context.isObject() && context.isMember("robots")) {
        if (!context["robots"].isObject())
            return false;
        for (const char* kind : {"money", "token", "xp"})
            if (!validNumber(context["robots"][kind], 1, 5))
                return false;
    }
    if (context.isObject() && context.isMember("specialization")) {
        if (!context["specialization"].isObject())
            return false;
        for (const char* kind : {"money", "token", "xp", "eggs"})
            if (!validNumber(context["specialization"][kind], .55, 1.60))
                return false;
    }
    for (const char* key : {"contract_serial", "expedition_serial"})
        if (context.isObject() && context.isMember(key) && !validNumber(context[key], 0, 1000000000, true))
            return false;
    if (context.isObject() && context.isMember("region") &&
        (!context["region"].isString() || context["region"].asString().size() > 32))
        return false;
    if (!context.isObject() || !context["enchants"].isObject() || context["enchants"].size() > 64 ||
        !validNumber(context["parts_prestige"], 0, 2147483646, true) ||
        !validNumber(context["prestige"], 0, 9e15, true))
        return false;
    for (const char* kind : {"money", "token", "xp"}) {
        const double cap = std::string(kind) == "money" ? 5000 : 3500;
        if (!validNumber(context[std::string(kind) + "_mult"], 0, cap) ||
            !validNumber(context[std::string("pace_") + kind], 0, 1.2))
            return false;
    }
    for (const auto& id : context["enchants"].getMemberNames()) {
        if (id.empty() || id.size() > 64 ||
            id.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos ||
            !validNumber(context["enchants"][id], 0, 1000000000, true))
            return false;
    }
    return true;
}
} // namespace

bool RewardQueue::enqueue(Game& game, Mine& mine, const Proc& proc, Cell origin, const Json& context) {
    if (jobs_.size() >= maximumJobs || !validSource(proc.id) || proc.level <= 0 || mine.resetting())
        return false;
    if (proc.id == "drill") {
        const auto low = mine.minimum(), high = mine.maximum();
        if (origin.x < low.x || origin.x > high.x || origin.y < low.y || origin.y > high.y ||
            origin.z < low.z || origin.z > high.z)
            return false;
        const int radius = proc.level >= 1500 ? 2 : proc.level >= 500 ? 1 : 0;
        Job job;
        job.source = proc.id;
        job.visual = game.state["message_settings"].get("visual_enchants", false) == true;
        job.low = {std::max(low.x, origin.x - radius), low.y, std::max(low.z, origin.z - radius)};
        job.high = {std::min(high.x, origin.x + radius), origin.y, std::min(high.z, origin.z + radius)};
        job.total = job.remaining =
            (job.high.x - job.low.x + 1) * (job.high.z - job.low.z + 1) * (job.high.y - job.low.y + 1);
        job.generation = mine.generation();
        job.context = context.isObject() ? context : game.captureRewardContext();
        jobs_.push_back(std::move(job));
        return true;
    }
    auto count = mine.enchantTargetCount(proc.id, proc.level, origin);
    if (proc.id == "veinminer") {
        const auto& config = game.data.enchants["effects"]["veinminer"];
        const auto scaled = std::floor(count * config.get("virtual_block_ratio", .20).asDouble() +
                                       std::generate_canonical<double, 53>(game.random));
        count = std::min<std::size_t>(config.get("virtual_block_cap", maximumBlocks).asUInt(),
                                      std::max(0.0, scaled));
    }
    count = std::min<std::size_t>(count, maximumBlocks);
    if (count == 0)
        return true;
    Job job;
    job.source = proc.id;
    job.total = job.remaining = static_cast<int>(count);
    job.compressed = std::clamp(
        static_cast<int>(std::floor(count * game.data.balance.get("proc_reward_ratio", .05).asDouble() + .5)),
        1, game.data.balance.get("proc_reward_block_cap", 250).asInt());
    job.generation = mine.generation();
    job.context = context.isObject() ? context : game.captureRewardContext();
    job.sample = mine.rewardSample();
    job.visual = game.state["message_settings"].get("visual_enchants", false) == true;
    if (job.visual) {
        job.origin = origin;
        job.low = mine.minimum();
        job.high = mine.maximum();
        if (!mine.inside(origin))
            return false;
        if (proc.id == "jackhammer")
            job.low.y = job.high.y = origin.y;
        else if (proc.id == "explosive") {
            const int radius = proc.level >= 2500 ? 2 : 1;
            job.cross = proc.level < 250;
            job.low = {std::max(job.low.x, origin.x - radius), std::max(job.low.y, origin.y - radius),
                       std::max(job.low.z, origin.z - radius)};
            job.high = {std::min(job.high.x, origin.x + radius), std::min(job.high.y, origin.y + radius),
                        std::min(job.high.z, origin.z + radius)};
        }
        job.terrainTotal =
            proc.id == "veinminer"
                ? job.total
                : (job.high.x - job.low.x + 1) * (job.high.y - job.low.y + 1) * (job.high.z - job.low.z + 1);
    }
    jobs_.push_back(std::move(job));
    return true;
}

std::size_t RewardQueue::step(Game& game, Mine& mine, std::size_t budget, double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds <= 0)
        return 0;
    budget = std::min<std::size_t>(budget, 512);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double, std::milli>(std::min(milliseconds, 10.0));
    std::size_t processed = 0;
    std::size_t terrainWork = 0;
    while (!jobs_.empty() && processed < budget && std::chrono::steady_clock::now() < deadline) {
        auto& job = jobs_.front();
        if (job.source == "drill") {
            if (job.remaining == 0 || job.generation != mine.generation() || game.shaftComplete()) {
                jobs_.pop_front();
                continue;
            }
            if (mine.resetting() || terrainWork >= 64)
                break;
            const int offset = job.total - job.remaining;
            const int width = job.high.x - job.low.x + 1, depth = job.high.z - job.low.z + 1;
            const Cell cell{job.low.x + offset % width, job.high.y - offset / (width * depth),
                            job.low.z + (offset / width) % depth};
            const auto block = job.visual ? mine.dig(cell) : mine.blockId(cell);
            if (!block.empty()) {
                game.mineBlock(block, false, false, "drill", job.context);
                game.recordShaftMining(1);
                if (!job.visual)
                    mine.applyVirtualMining(1, job.generation);
            }
            --job.remaining;
            ++processed;
            ++terrainWork;
            continue;
        }
        if (job.remaining > 0) {
            const bool ore =
                std::generate_canonical<double, 53>(game.random) < job.sample["ore_ratio"].asDouble();
            game.mineBlock(job.sample[ore ? "ore" : "smooth"].asString(), false, false, job.source,
                           job.context);
            --job.remaining;
            // spread compressed progression across payouts; remaining also persists across saves.
            const int completed = job.total - job.remaining;
            if (completed * job.compressed / job.total > (completed - 1) * job.compressed / job.total)
                game.recordMining(1, ore ? 1 : 0, job.context);
            ++processed;
        }
        if (job.remaining == 0) {
            // a restored mine may still be building when its last reward is paid.
            if (job.generation == mine.generation() && mine.resetting())
                break;
            if (!job.rewardsFinished) {
                game.finishProc(job.total, job.compressed, job.context);
                if (!job.visual)
                    mine.applyVirtualMining(job.compressed, job.generation);
                job.rewardsFinished = true;
            }
            if (job.visual && job.generation == mine.generation() && !game.shaftComplete() &&
                job.terrainCursor < job.terrainTotal) {
                if (terrainWork >= 64 || processed >= budget)
                    break;
                Cell cell;
                bool target = true;
                if (job.source == "veinminer")
                    target = mine.nextTarget(cell);
                else {
                    const int offset = job.terrainCursor;
                    const int width = job.high.x - job.low.x + 1, depth = job.high.z - job.low.z + 1;
                    cell = {job.low.x + offset % width, job.high.y - offset / (width * depth),
                            job.low.z + (offset / width) % depth};
                    if (job.source == "explosive") {
                        const int dx = cell.x - job.origin.x, dy = cell.y - job.origin.y,
                                  dz = cell.z - job.origin.z;
                        target = (dx != 0 || dy != 0 || dz != 0) &&
                                 (!job.cross || (dy == 0 && std::abs(dx) + std::abs(dz) <= 1));
                    }
                }
                // terrain follows the paid proc; digging here must not pay it a second time.
                if (target && !mine.dig(cell).empty())
                    game.recordShaftMining(1);
                ++job.terrainCursor;
                ++terrainWork;
                ++processed;
                if (job.terrainCursor < job.terrainTotal)
                    continue;
            }
            jobs_.pop_front();
        }
    }
    return processed;
}

Json RewardQueue::snapshot(const Mine& mine) const {
    Json value;
    value["version"] = 2;
    value["jobs"] = Json(::Json::arrayValue);
    for (const auto& job : jobs_) {
        Json row;
        row["source"] = job.source;
        row["total"] = job.total;
        row["remaining"] = job.remaining;
        row["compressed"] = job.compressed;
        row["visual"] = job.visual;
        row["finished"] = job.rewardsFinished;
        row["terrain_cursor"] = job.terrainCursor;
        row["terrain_pending"] = job.generation == mine.generation();
        row["context"] = job.context;
        row["sample"] = job.sample;
        if (job.source == "drill" || job.visual) {
            for (const int n : {job.low.x, job.low.y, job.low.z})
                row["low"].append(n);
            for (const int n : {job.high.x, job.high.y, job.high.z})
                row["high"].append(n);
        }
        if (job.visual && job.source != "drill") {
            for (const int n : {job.origin.x, job.origin.y, job.origin.z})
                row["origin"].append(n);
            row["cross"] = job.cross;
        }
        value["jobs"].append(std::move(row));
    }
    return value;
}

bool RewardQueue::restore(const Json& value, Mine& mine) {
    if (value.isNull()) {
        clear();
        return true;
    }
    if (!value.isObject() || !validNumber(value["version"], 1, 2, true) || !value["jobs"].isArray() ||
        value["jobs"].size() > maximumJobs)
        return false;
    std::deque<Job> restored;
    for (const auto& row : value["jobs"]) {
        if (!row.isObject() || !row["source"].isString() || !validSource(row["source"].asString()) ||
            !validNumber(row["total"], 1, maximumBlocks, true) ||
            !validNumber(row["remaining"], 0, row["total"].asDouble(), true) ||
            !validNumber(row["compressed"], 0, 250, true) || !row["terrain_pending"].isBool() ||
            !validContext(row["context"]))
            return false;
        Job job;
        job.source = row["source"].asString();
        job.total = row["total"].asInt();
        job.remaining = row["remaining"].asInt();
        job.compressed = row["compressed"].asInt();
        if (value["version"].asInt() == 2) {
            if (!row["visual"].isBool() || !row["finished"].isBool() ||
                !validNumber(row["terrain_cursor"], 0, 1000000, true))
                return false;
            job.visual = row["visual"].asBool();
            job.rewardsFinished = row["finished"].asBool();
            job.terrainCursor = row["terrain_cursor"].asInt();
            if ((job.rewardsFinished && job.remaining != 0) ||
                (job.terrainCursor > 0 && (!job.visual || !job.rewardsFinished || job.source == "drill")))
                return false;
        } else
            job.visual = job.source == "drill";
        if (job.source == "drill" && !row["terrain_pending"].asBool())
            continue;
        if (job.source == "drill" || job.visual) {
            const auto low = mine.minimum(), high = mine.maximum();
            const int minimum[] = {low.x, low.y, low.z}, maximum[] = {high.x, high.y, high.z};
            for (const char* key : {"low", "high"}) {
                if (!row[key].isArray() || row[key].size() != 3)
                    return false;
                for (int i = 0; i < 3; ++i) {
                    if (!validNumber(row[key][i], -10000, 10000, true))
                        return false;
                    if (row["terrain_pending"].asBool() &&
                        !validNumber(row[key][i], minimum[i], maximum[i], true))
                        return false;
                }
            }
            job.low = {row["low"][0].asInt(), row["low"][1].asInt(), row["low"][2].asInt()};
            job.high = {row["high"][0].asInt(), row["high"][1].asInt(), row["high"][2].asInt()};
            const int w = job.high.x - job.low.x + 1, d = job.high.z - job.low.z + 1,
                      h = job.high.y - job.low.y + 1;
            const auto volume = static_cast<std::int64_t>(w) * d * h;
            if (w < 1 || d < 1 || h < 1 || volume > 1000000)
                return false;
            if (job.source == "drill") {
                if (w > 5 || d > 5 || job.compressed != 0 || volume != job.total || job.rewardsFinished)
                    return false;
            } else {
                if (!row["origin"].isArray() || row["origin"].size() != 3 || !row["cross"].isBool())
                    return false;
                for (int i = 0; i < 3; ++i)
                    if (!validNumber(row["origin"][i], row["low"][i].asDouble(), row["high"][i].asDouble(),
                                     true))
                        return false;
                job.origin = {row["origin"][0].asInt(), row["origin"][1].asInt(), row["origin"][2].asInt()};
                job.cross = row["cross"].asBool();
                if ((job.source != "explosive" && job.cross) ||
                    (job.source == "explosive" && (w > 5 || d > 5 || h > 5)) ||
                    (job.source == "jackhammer" && h != 1))
                    return false;
                job.terrainTotal = job.source == "veinminer" ? job.total : int(volume);
                if (job.terrainCursor > job.terrainTotal)
                    return false;
            }
        }
        if (job.source != "drill" &&
            (!validSample(row["sample"]) ||
             job.compressed != std::clamp(static_cast<int>(std::floor(job.total * .05 + .5)), 1, 250)))
            return false;
        job.generation = row["terrain_pending"].asBool() ? mine.generation() : 0;
        job.context = row["context"];
        job.sample = row["sample"];
        restored.push_back(std::move(job));
    }
    jobs_ = std::move(restored);
    return true;
}
} // namespace prison
