#include "prison/bombs.hpp"
#include "prison/game.hpp"
#include "prison/save.hpp"
#include "prison/world.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace prison;

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void finish(Mine& mine) {
    int steps = 0;
    while (mine.resetting() || mine.pendingChunks()) {
        mine.step(128, 2);
        check(++steps < 10000, "mine work failed to finish");
    }
}

prison::Json item(const char* id, int count = 1) {
    prison::Json value;
    value["id"] = id;
    value["count"] = count;
    return value;
}

int findItem(const Game& game, const std::string& id) {
    const auto& slots = game.state["inventory_slots"];
    for (unsigned i = 1; i < slots.size(); ++i)
        if (slots[i].isObject() && slots[i].get("id", "").asString() == id)
            return int(i);
    return -1;
}

void fillInventory(Game& game) {
    for (int i = 1; i < 36; ++i) {
        prison::Json value = item("pet");
        value["uid"] = "full-" + std::to_string(i);
        value["tier_id"] = "common";
        game.state["inventory_slots"][i] = value;
    }
}

struct TemporarySaves {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("prisoncpp-integration-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporarySaves() {
        std::filesystem::create_directories(root);
    }
    ~TemporarySaves() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void dataRevisions(const Data& source, const std::filesystem::path& root) {
    std::filesystem::copy(source.root / "data", root / "data", std::filesystem::copy_options::recursive);
    for (const char* name : {"source_systems.json", "native_economy.json", "native_content.json"}) {
        const auto path = root / "data" / name;
        const auto original = readJson(path);
        auto old = original;
        old.removeMember("data_revision");
        {
            std::ofstream out(path);
            out << jsonText(old);
        }
        bool rejected = false;
        try {
            Data pack;
            pack.load(root);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "an old table cannot restore retired content or cheap gem prices");
        {
            std::ofstream out(path);
            out << jsonText(original);
        }
    }
    Data pack;
    pack.load(root);
    check(pack.systems == source.systems && pack.enchants == source.enchants,
          "matching data revisions load unchanged");
}

void discoveries(const Data& source) {
    Data data = source;
    data.systems["discoveries"]["pocket_chance"] = 1;
    Mine mine(data);
    mine.reset(0, 0, 72);
    finish(mine);
    auto saved = mine.snapshot();
    check(saved["pocket_cells"].size() == 5 && !saved.isMember("cave") && !saved.isMember("crate"),
          "ore pockets remain without cave or crate generation");
    check(saved["cells"].asString().find('0') == std::string::npos, "new mines contain no carved caves");
    Game game(data);
    int mined = 0;
    Cell first;
    for (const auto& row : saved["pocket_cells"]) {
        const int at = row.asInt();
        Cell cell{mine.minimum().x + at % mine.width(), mine.minimum().y + at / (mine.width() * mine.depth()),
                  mine.minimum().z + at / mine.width() % mine.depth()};
        if (mined++ == 0)
            first = cell;
        check(!mine.dig(cell).empty(), "rare ore cell is mineable");
    }
    check(mine.dig(first).empty(), "remining cannot duplicate a pocket drop");
    const auto pending = mine.snapshot();
    check(pending["pocket_cells"].empty() && pending["pending_pocket_drops"].asInt() == 5,
          "mined rare cells become exactly one saved delivery each");
    Mine restored(data);
    check(restored.loadSnapshot(pending, 0), "discovery snapshot reloads");
    game.awardPocketDrops(restored.takePocketDrops());
    check(restored.snapshot()["pending_pocket_drops"].asInt() == 0,
          "resaving during restore cannot duplicate drops");
    finish(restored);
    game.awardPocketDrops(restored.takePocketDrops());
    int crystals = findItem(game, "crystal");
    check(crystals > 0 && game.state["inventory_slots"][crystals]["count"].asInt() == 5,
          "pockets award each crystal once");
    Game resumed(data);
    resumed.loadSnapshot(game.snapshot());
    check(resumed.state["inventory_slots"] == game.state["inventory_slots"], "pocket drops persist");
    Mine revisited(data);
    auto legacy = restored.snapshot();
    legacy.removeMember("pocket_cells");
    legacy.removeMember("pending_pocket_drops");
    legacy["cave"]["centre"].append(0);
    legacy["cave"]["centre"].append(mine.minimum().y + 2);
    legacy["cave"]["centre"].append(0);
    legacy["cave"]["found"] = true;
    legacy["crate"].append(1);
    legacy["crate"].append(mine.minimum().y + 1);
    legacy["crate"].append(0);
    check(revisited.loadSnapshot(legacy, 0), "old mine snapshots remain supported");
    check(!revisited.snapshot().isMember("cave") && !revisited.snapshot().isMember("crate"),
          "retired features are removed even while restoring");
    finish(revisited);
    check(revisited.snapshot()["cells"] == legacy["cells"] && revisited.snapshot()["pocket_cells"].empty(),
          "old excavations do not gain retroactive pockets");
    auto bad = pending;
    bad["pending_pocket_drops"] = -1;
    check(!revisited.loadSnapshot(bad, 0), "invalid pending drops rejected before changing terrain");

    Data distribution = source;
    for (const char* axis : {"x", "z"}) {
        distribution.mine["mine_min"][axis] = -4;
        distribution.mine["mine_max"][axis] = 4;
    }
    Mine natural(distribution);
    int pockets = 0;
    for (std::uint64_t seed = 1; seed <= 2000; ++seed) {
        natural.reset(0, 0, seed);
        finish(natural);
        const auto state = natural.snapshot();
        pockets += !state["pocket_cells"].empty();
        check(!state.isMember("cave") && !state.isMember("crate"), "refills cannot generate retired features");
    }
    check(pockets > 0 && pockets < 16, "ore pockets must remain very rare across seeded mines");
}

void virtualAutominer(const Data& source) {
    Data data = source;
    data.enchants["effects"]["egg_drop"]["blocks_per_drop"] = 1;
    Game game(data);
    Mine mine(data);
    mine.reset(0, 0, 181);
    finish(mine);
    const auto cells = mine.snapshot()["cells"];
    auto sample = mine.rewardSample();
    sample["ore_ratio"] = 0;
    game.state["autominer_time"] = 10;
    game.state["enchants"]["timefinder"] = 10000;
    game.state["pet_data"]["hatchery"][0]["required"] = 25000;
    game.state["pet_data"]["hatchery"][0]["progress"] = 0;
    check(game.command("/am start").ok, "autominer starts holding pickaxe");
    check(game.autominerTickCost() == 5, "three BPS costs five banked seconds per tick");
    check(game.tickAutominer(.5, sample) == 0, "partial autominer tick cannot pay or consume time");
    game.tick(.5);
    check(game.state["autominer_time"].asDouble() == 10, "general timer cannot drain unpaid autominer time");
    int paid = game.tickAutominer(.5, sample);
    check(paid == 3 && game.state["autominer_time"].asDouble() == 5, "autominer pays one complete tick");
    mine.applyVirtualMining(paid, mine.generation());
    paid = game.tickAutominer(1, sample);
    mine.applyVirtualMining(paid, mine.generation());
    check(paid == 3 && !game.state["autominer_active"].asBool(),
          "last affordable tick is paid before stopping");
    check(game.state["blocks_mined"].asInt() == 6 && game.state["money"].asDouble() == 570,
          "virtual mining pays source rewards once per virtual block");
    check(game.state["pet_data"]["hatchery"][0]["progress"].asDouble() == 6, "autominer advances hatchery");
    check(mine.snapshot()["cells"] == cells && mine.snapshot()["virtual_mined"].asInt() == 6,
          "autominer advances mine progress without removing terrain");
    check(findItem(game, "pet_egg") < 0 && findItem(game, "automine_time") < 0,
          "autominer cannot generate normal eggs or its own time");
    game.state["autominer_time"] = 4;
    check(!game.command("/am start").ok && game.state["autominer_time"].asDouble() == 0,
          "insufficient bank cannot buy a partial tick");
    game.state["autominer_time"] = 100;
    game.state["selected_hotbar_index"] = 8;
    check(!game.command("/am start").ok, "start requires held pickaxe");
    game.state["selected_hotbar_index"] = 0;
    check(game.command("/am start").ok && game.tickAutominer(15, sample) == 3,
          "long stalls never create a backlog of autominer rewards");
    auto saved = game.snapshot();
    check(!saved["autominer_active"].asBool() && game.state["autominer_active"].asBool(),
          "autosave does not stop the live autominer or restart it on login");
    saved["autominer_active"] = true;
    Game resumed(data);
    resumed.loadSnapshot(saved);
    check(!resumed.state["autominer_active"].asBool() &&
              resumed.state["autominer_time"] == saved["autominer_time"],
          "legacy active saves load stopped and preserve banked time");
    game.state["selected_hotbar_index"] = 8;
    check(game.tickAutominer(1, sample) == 0 && !game.state["autominer_active"].asBool(),
          "switching away from the pickaxe stops automining");
    mine.reset(0, 0, 182);
    mine.applyVirtualMining(3, mine.generation());
    finish(mine);
    check(mine.snapshot()["virtual_mined"].asInt() == 3, "refills retain concurrent virtual progress");
}

void savedMining(const Data& data, const std::filesystem::path& root) {
    Game game(data);
    game.random.seed(10);
    Mine mine(data);
    mine.reset(0, 0, 12345);
    finish(mine);
    const auto initial = mine.remaining();
    int removed = 0;
    for (int x = -8; x <= 8; ++x) {
        const Cell cell{x, -1, 0};
        const auto block = mine.dig(cell);
        check(!block.empty() && game.mineBlock(block, false).ok, "manual mining pipeline failed");
        ++removed;
    }
    check(mine.queueBlast({0, -2, 0}, 2), "bomb queue rejected");
    while (mine.pendingBlasts()) {
        const auto batch = mine.advanceBlasts(3);
        check(batch.size() <= 3, "bomb work exceeded frame budget");
        for (const auto& cell : batch) {
            check(game.mineBlock(cell.block, false, false, "minebomb").ok, "bomb reward failed");
            ++removed;
        }
    }
    check(mine.remaining() + removed == initial, "terrain removal disagrees with rewarded blocks");
    check(game.state["blocks_mined"].asInt() == removed, "bomb rewards duplicated mined blocks");
    finish(mine);
    game.state["mine_state"] = mine.snapshot();
    game.state["future_extension"]["metadata"] = "preserve";
    const auto before = game.snapshot();
    SaveStore saves(root / "mining");
    saves.save("world", "test world", before);
    Game resumed(data);
    resumed.loadSnapshot(saves.load("world")["state"]);
    Mine restored(data);
    check(restored.loadSnapshot(resumed.state["mine_state"], resumed.state["rank_index"].asInt()),
          "saved excavations rejected");
    finish(restored);
    check(restored.snapshot()["cells"] == before["mine_state"]["cells"], "login regenerated mined terrain");
    check(resumed.state["money"] == before["money"] && resumed.state["tokens"] == before["tokens"],
          "save changed currency");
    check(resumed.state["future_extension"] == before["future_extension"], "save discarded unknown metadata");
    const auto savedBlocks = resumed.state["blocks_mined"];
    check(restored.dig({0, -1, 0}).empty() && resumed.state["blocks_mined"] == savedBlocks,
          "removed cell became mineable after login");

    resumed.state["money"] = resumed.rankCost(1);
    auto result = resumed.command("/rankup");
    check(result.ok && result.resetMine, "rank command did not request a refill");
    restored.reset(resumed.state["rank_index"].asInt(), resumed.state["total_prestige"].asInt64());
    check(restored.dig({0, -1, 0}).empty(), "mining allowed during rank refill");
    finish(restored);
    const auto ranked = restored.blockId({0, -1, 0});
    check(ranked == "tier_b_smooth" || ranked == "tier_b_ore", "rank refill used previous rank blocks");
    check(resumed.state["money"].asDouble() == 0, "rank currency transaction differs from terrain rank");
}

void transfersAndFilters(const Data& data, const std::filesystem::path& root) {
    Game game(data);
    game.state["cleaner_settings"]["enabled"] = true;
    game.state["cleaner_settings"]["mode"] = "blacklist";
    game.state["cleaner_settings"]["default_action"] = "salvage";
    prison::Json pet = item("pet");
    pet["uid"] = "pet-with-metadata";
    pet["tier_id"] = "mythical";
    pet["pet_type"] = "Sheep";
    pet["pet_color"] = "#4488aa";
    pet["boost_type"] = "exp";
    pet["base_boost_pct"] = 42.5;
    pet["level"] = 12;
    pet["exp"] = 37;
    pet["custom_metadata"]["value"] = "retain";
    check(game.giveItem(pet, false), "manual pet delivery failed");
    check(game.useItem(findItem(game, "pet")).ok, "pet equip failed");
    check(game.state["pet_data"]["active_pet"] == pet, "equipping changed pet metadata");
    check(game.transfer("pet", 0, "vault", 0).ok, "pet to vault failed");
    check(game.transfer("vault", 0, "inventory", 5).ok, "vault return failed");
    check(game.state["inventory_slots"][5] == pet && game.state["tokens"].asDouble() == 0,
          "filter intercepted manual pet return");

    prison::Json pouch = item("pouch");
    pouch["tier"] = "small";
    pouch["slots"] = 18;
    pouch["uid"] = "pouch-with-metadata";
    pouch["items"] = prison::Json(::Json::arrayValue);
    check(game.giveItem(pouch), "pouch grant failed");
    const int pouchIndex = findItem(game, "pouch");
    const std::string container = "pouch:" + std::to_string(pouchIndex);
    check(game.transfer("inventory", 5, container, 0).ok, "pet storage in pouch failed");
    check(game.state["inventory_slots"][5].isNull(), "pouch deposit duplicated pet");
    check(game.transfer(container, 0, "inventory", 5).ok, "pet withdrawal from pouch failed");
    check(game.state["inventory_slots"][pouchIndex]["items"][0].isNull(), "pouch withdrawal duplicated pet");
    check(game.state["inventory_slots"][5] == pet && game.state["tokens"].asDouble() == 0,
          "pouch return lost metadata or used filter");
    const auto beforeNested = game.snapshot();
    check(!game.transfer("inventory", pouchIndex, container, 1).ok && game.snapshot() == beforeNested,
          "pouch accepted itself");

    check(game.transfer("inventory", 5, "pet", 0).ok, "pet reequip failed");
    fillInventory(game);
    const auto full = game.snapshot();
    check(!game.transfer("pet", 0, "inventory").ok && game.snapshot() == full,
          "full inventory deleted active pet");
    game.state["inventory_slots"][5] = prison::Json();
    check(game.transfer("pet", 0, "inventory", 5).ok, "pet return failed after freeing space");
    check(game.state["inventory_slots"][5] == pet, "pet return changed metadata");
    const auto tokens = game.state["tokens"].asDouble();
    prison::Json egg = item("pet_egg");
    egg["tier_id"] = "common";
    check(game.giveItem(egg, true) && game.state["tokens"].asDouble() > tokens,
          "automatic incoming egg ignored filter");
    check(game.state["inventory_slots"][5] == pet, "automatic filtering consumed existing pet");

    SaveStore saves(root / "items");
    saves.save("items", "items", game.snapshot());
    Game loaded(data);
    loaded.loadSnapshot(saves.load("items")["state"]);
    check(loaded.state["inventory_slots"][5]["uid"] == pet["uid"] &&
              loaded.state["inventory_slots"][5]["custom_metadata"] == pet["custom_metadata"],
          "item metadata did not survive save");
    for (const int index : {-1, 36, 1000000}) {
        const auto before = loaded.snapshot();
        check(!loaded.transfer("inventory", index, "inventory", 0).ok && loaded.snapshot() == before,
              "out-of-range transfer mutated inventory");
    }
}

void delayedRewards(const Data& data) {
    Game game(data);
    game.state["enchants"]["hatchery"] = 5;
    const auto context = game.captureRewardContext();
    game.state["enchants"]["experienced"] = 1000;
    game.state["enchants"]["hatchery"] = 0;
    game.state["pick_level"] = 4000;
    const auto reward = game.mineBlock("tier_a_smooth", false, false, "explosive", context);
    check(reward.money == 95 && std::abs(reward.xp - .8143) < 1e-9,
          "queued reward used new levels instead of captured context");
    check(game.state["pick_level"].asInt() == 4000, "captured reward overwrote live pick level");
    prison::Json egg = item("pet");
    egg["required"] = 1000;
    egg["progress"] = 0;
    egg["ready"] = false;
    game.state["pet_data"]["hatchery"].append(egg);
    game.finishProc(49, 1, context);
    check(game.state["pet_data"]["hatchery"][0]["progress"].asInt() == 57,
          "queued hatchery progress did not use captured hatchery level");

    game.newWorld();
    const auto bombContext = game.captureRewardContext();
    prison::Json sample;
    sample["smooth"] = "tier_a_smooth";
    sample["ore"] = "tier_a_ore";
    sample["ore_ratio"] = 0;
    const auto before = game.snapshot();
    game.awardBomb(0, 3, 1, bombContext, sample);
    check(game.snapshot() == before, "bomb paid rewards without removing terrain");
    game.awardBomb(10, 3, 1, bombContext, sample);
    check(game.state["blocks_mined"].asInt() == 10 && game.state["money"].asDouble() == 285,
          "bomb reward count did not distinguish samples from removed cells");
    check(game.state["xp"].asDouble() == 0 && game.state["pick_xp"].asDouble() == 0,
          "bomb awarded pickaxe experience");

    game.newWorld();
    Mine mine(data);
    Bombs bombs;
    mine.reset(0, 0, 12345);
    check(!bombs.launch(mine, game, {0, 4, 0}, {0, -1, 0}, 1).ok, "bomb launched during refill");
    finish(mine);
    const auto initial = mine.remaining();
    check(bombs.launch(mine, game, {0, 4, 0}, {0, -1, 0}, 1).ok, "bomb launch failed");
    check(!bombs.launch(mine, game, {0, 4, 0}, {0, -1, 0}, 1).ok, "bomb cooldown was bypassed");
    for (int frame = 0; frame < 150; ++frame)
        bombs.tick(mine, game, .02);
    check(initial - mine.remaining() == 6 && game.state["blocks_mined"].asInt() == 6,
          "tier one projectile explosion radius or reward count changed");
    check(game.state["pick_xp"].asDouble() == 0, "projectile bomb awarded pickaxe experience");
    check(bombs.launch(mine, game, {0, 20, 0}, {0, 1, 0}, 1).ok, "second bomb did not clear cooldown");
    mine.reset(0, 0, 54321);
    bombs.tick(mine, game, .02);
    finish(mine);
    const auto fresh = mine.remaining();
    for (int frame = 0; frame < 250; ++frame)
        bombs.tick(mine, game, .02);
    check(mine.remaining() == fresh && game.state["blocks_mined"].asInt() == 6,
          "stale projectile damaged or rewarded a new mine generation");
    game.state["autominer_active"] = true;
    check(!bombs.launch(mine, game, {0, 4, 0}, {0, -1, 0}, 1).ok,
          "bomb bypassed autominer mining restriction");
}

void importsAndCommands(const Data& data, const std::filesystem::path& root) {
    Game game(data);
    for (const char* command :
         {"/test max", "/givecrystal 10", "/givepet common", "/picklevel 5000", "/test stress mine"}) {
        const auto before = game.snapshot();
        check(!game.command(command).ok && game.snapshot() == before,
              "developer command changed ordinary world");
    }
    game.newWorld(true);
    for (const char* command :
         {"/givecrystal nan", "/givecrystal 1 -1", "/givepart sell 2 invalid", "/givetime 999999999999h",
          "/givebomb 21 1", "/picklevel 2147483648", "/pickenchant fortune inf",
          "/enchantcolor \\000000 #000000", "/pv ../world"}) {
        const auto before = game.snapshot();
        check(!game.command(command).ok && game.snapshot() == before, "invalid command mutated state");
    }
    check(!game.command("/prestigemax").ok && !game.command("/gang").ok, "removed commands became active");
    auto help = game.command("/help crystals");
    check(help.menu == "help" && help.message.empty() && game.state["_help_search"] == "crystals",
          "help must pass search to its menu without flooding chat");

    prison::Json saved = game.snapshot();
    saved.removeMember("devmode");
    saved["playtime_seconds"] = 1234.75;
    saved["_stress"]["active"] = true;
    saved["_stress"]["type"] = "prestige";
    saved["_rename_target"] = "pickaxe";
    saved["settings"] = false;
    saved["inventory_slots"][2] = item("pet");
    saved["inventory_slots"][2]["level"] = 1e100;
    saved["inventory_slots"][2]["exp"] = 1e100;
    saved["inventory_slots"][2]["tier"] = 1e100;
    saved["inventory_slots"][2]["description"] = prison::Json(::Json::arrayValue);
    saved["inventory_slots"][2]["enchanted"] = "yes";
    saved["boosts"]["money"].append(2.0);
    prison::Json envelope;
    envelope["schema_version"] = 3;
    envelope["state"] = saved;
    envelope["external_metadata"]["version"] = "future";
    const auto importPath = root / "godot-export.json";
    const auto original = jsonText(envelope);
    {
        std::ofstream out(importPath);
        out << original;
    }
    SaveStore saves(root / "imports");
    const auto imported = saves.importFile(importPath);
    check(jsonText(readJson(importPath)) == original, "import modified source world");
    game.loadSnapshot(imported["state"]);
    check(game.state["playtime_seconds"] == saved["playtime_seconds"], "import truncated playtime");
    check(!game.state["devmode"].asBool() && !game.state.isMember("_stress") &&
              !game.state.isMember("_rename_target"),
          "import enabled developer mode or transient actions");
    check(game.state["settings"].isObject(), "malformed settings survived import");
    check(!game.itemLore(game.state["inventory_slots"][2]).empty(), "normalized item lore failed");
    check(game.state["inventory_slots"][2]["level"].asInt() == 75 &&
              game.state["inventory_slots"][2]["exp"].asInt() == 99,
          "pet progression exceeds numeric bounds");
    prison::Json multiplier = item("multiplier");
    multiplier["amount"] = 2.0;
    multiplier["duration"] = 60.25;
    multiplier["boost_type"] = "sell";
    check(game.giveItem(multiplier) && game.useItem(findItem(game, "multiplier")).ok,
          "legacy boost prevented multiplier activation");
    check(game.command("/endmulti").ok, "legacy boost prevented multiplier return");
    const auto updated = saves.save(imported["slot"]["id"].asString(), "imported", game.snapshot());
    check(updated["external_metadata"] == envelope["external_metadata"],
          "saving imported envelope dropped extension metadata");

    for (const std::string& id :
         std::vector<std::string>{"", "..", "../escape", "bad/name", std::string(65, 'x')}) {
        bool rejected = false;
        try {
            saves.save(id, "invalid", game.snapshot());
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "save id escaped or exceeded save namespace");
    }
    saves.save("recovery", "recovery", game.snapshot());
    game.state["money"] = 20;
    saves.save("recovery", "recovery", game.snapshot());
    {
        std::ofstream out(root / "imports/recovery.json");
        out << "{broken";
    }
    check(saves.load("recovery")["state"]["money"].asDouble() == 0,
          "corrupt save did not recover previous complete backup");
    prison::Json malformed = envelope;
    malformed["slot"]["id"] = "malformed";
    malformed["slot"]["last_saved_at"] = "bad timestamp";
    {
        std::ofstream out(root / "imports/malformed.json");
        out << jsonText(malformed);
    }
    check(!saves.list().empty(), "malformed slot metadata broke world listing");
    malformed["schema_version"] = 4;
    {
        std::ofstream out(root / "future.json");
        out << jsonText(malformed);
    }
    bool rejected = false;
    try {
        saves.importFile(root / "future.json");
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "newer save schema was imported unsafely");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Data data;
        data.load(argc > 1 ? argv[1] : "..");
        TemporarySaves saves;
        dataRevisions(data, saves.root);
        discoveries(data);
        virtualAutominer(data);
        savedMining(data, saves.root);
        transfersAndFilters(data, saves.root);
        delayedRewards(data);
        importsAndCommands(data, saves.root);
        std::cout << "integration: mining/save reload, atomic transfers, filter isolation, command gates and "
                     "malformed imports passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "integration test: " << error.what() << '\n';
        return 1;
    }
}
