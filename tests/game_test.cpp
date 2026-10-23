#include "prison/game.hpp"
#include "prison/guardians.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    if (std::abs(actual - expected) > std::max(1e-9, std::abs(expected) * 1e-11))
        throw std::runtime_error(message);
}
prison::Json equipped(const char* id, const char* kind, double boost, int tier = 1) {
    prison::Json value;
    value["id"] = id;
    value["boost_type"] = kind;
    value["boost"] = boost;
    value["tier"] = tier;
    value["count"] = 1;
    value["identified"] = true;
    return value;
}

void tutorial(const prison::Data& data) {
    prison::Game game(data);
    check(game.state["tutorial"]["status"] == "new", "new worlds offer onboarding");
    const auto inventory = game.state["inventory_slots"];
    check(game.command("/tutorial skip").ok, "tutorial can be skipped immediately");
    game.loadSnapshot(game.snapshot());
    check(game.state["tutorial"]["status"] == "skipped" && game.state["inventory_slots"] == inventory,
          "skipping persists without taking or granting items");
    check(game.command("/tutorial restart").ok, "skipped tutorial can restart");
    game.mineBlock("tier_a_smooth", false, false, "drill");
    check(game.state["tutorial"]["mined"] == 0, "automatic blocks cannot complete the mining lesson");
    for (int i = 0; i < 10; ++i)
        game.mineBlock("tier_a_smooth", false);
    check(game.tutorialStep() == 2, "manual mining advances the lesson");
    game.tutorialEvent("inventory");
    game.loadSnapshot(game.snapshot());
    check(game.tutorialStep() == 3 && game.state["tutorial"]["status"] == "active",
          "unfinished tutorial resumes after loading");
    check(!game.buyEnchant("efficiency").ok && game.tutorialStep() == 3,
          "failed purchases cannot complete the enchant lesson");
    game.state["tokens"] = game.enchantCost("efficiency");
    check(game.buyEnchant("efficiency").ok && game.tutorialStep() == 4, "purchasing advances the lesson");
    game.state["money"] = game.rankCost(1);
    check(game.rankUp().ok && game.state["tutorial"]["status"] == "completed",
          "ranking up completes onboarding");
    game.loadSnapshot(game.snapshot());
    check(game.state["tutorial"]["status"] == "completed", "completed onboarding stays complete");
    auto legacy = game.snapshot();
    legacy.removeMember("tutorial");
    game.loadSnapshot(legacy);
    check(game.state["tutorial"]["status"] == "skipped", "old worlds are not forced into onboarding");
    game.newWorld(true);
    check(game.state["tutorial"]["status"] == "skipped", "development worlds can opt in");
}

void nativeContent(prison::Data data) {
    prison::Game game(data);
    game.state["pick_level"] = 200;
    for (int i = 0; i < 3; ++i)
        game.state["equipped_crystals"][i] = equipped("crystal", "sell", 0, 5);
    check(game.crystalResonance()["id"] == "prosperity", "matching crystals activate resonance");
    near(game.boost("money"), 1.04, "resonance applies once");
    game.state["equipped_crystals"][1]["tier"] = 6;
    check(game.crystalResonance().isNull(), "mixed tiers cannot resonate");
    game.state["equipped_crystals"][1]["tier"] = 5;
    game.state["equipped_crystals"][1]["boost_type"] = "exp";
    game.state["equipped_crystals"][2]["boost_type"] = "token";
    check(game.crystalResonance()["id"] == "harmony", "mixed boost order activates harmony");
    near(game.boost("money"), 1.02, "harmony uses its smaller bonus");
    game.state["equipped_crystals"][0]["identified"] = false;
    check(game.crystalResonance().isNull(), "unidentified crystal cannot resonate");
    game.state["equipped_crystals"][0]["identified"] = true;
    game.state["pick_level"] = 79;
    check(game.crystalResonance().isNull(), "locked slots cannot resonate");

    game.newWorld();
    data.systems["mutations"]["chance"] = 1.0;
    auto pet = equipped("pet", "exp", 0);
    pet["tier_id"] = "mythical";
    pet["required"] = 1;
    game.state["pet_data"]["hatchery"][0] = pet;
    game.tickItems(0, 1);
    const auto hatched = game.state["pet_data"]["hatchery"][0];
    check(game.petMutation(hatched).isObject(), "finished hatch rolls a mutation");
    game.tickItems(0, 10);
    check(game.state["pet_data"]["hatchery"][0] == hatched, "ready hatch cannot reroll");
    game.loadSnapshot(game.snapshot());
    check(game.state["pet_data"]["hatchery"][0]["mutation"] == hatched["mutation"],
          "mutation survives reload");
    for (int i = 1; i <= 5; ++i) {
        pet["mutation"] = i <= 2 ? "efficient" : i == 3 ? "brood" : "none";
        game.state["inventory_slots"][i] = pet;
    }
    check(game.itemAction("merge_pets").ok, "mutated pets can merge");
    check(game.state["inventory_slots"][1]["mutation"] == "efficient" &&
              game.state["inventory_slots"][1]["mutation_rolled"].asBool(),
          "merge inherits the most frequent trait without rerolling");

    pet["mutation"] = "efficient";
    for (int level : {1, 4000})
        for (int fuel : {0, 1, 2, 10, 25})
            for (int credit : {0, 95}) {
                prison::Game single(data), batch(data);
                auto robot = game.makeRobot("sell");
                robot["level"] = level;
                robot["fuel"] = fuel;
                robot["fuel_credit"] = credit;
                single.state["robot_slots"][0] = batch.state["robot_slots"][0] = robot;
                single.state["pet_data"]["active_pet"] = batch.state["pet_data"]["active_pet"] = pet;
                double total = 0;
                for (int i = 0; i < 64; ++i)
                    total += single.consumeRobotFuel(1)["money"].asDouble();
                near(batch.consumeRobotFuel(64)["money"].asDouble(), total / 64,
                     "batching cannot change powered rewards");
                check(single.state["robot_slots"] == batch.state["robot_slots"],
                      "batching cannot change remaining fuel or fractional savings");
            }

    game.newWorld();
    check(!game.command("/test guardian").ok && !game.spawnGuardian({}, true),
          "ordinary worlds cannot force guardians");
    data.systems["guardians"]["chance"] = 1.0;
    game.state["pick_level"] = 999;
    check(!game.spawnGuardian({}), "guardian pickaxe-level gate");
    game.state["pick_level"] = 1000;
    check(game.spawnGuardian({}), "eligible manual encounter can spawn");
    const auto& guardian = game.state["guardian"];
    check(prison::raycastGuardian(guardian, {-3, 1.5f, 0}, {1, 0, 0}, 8).part == 4,
          "shared guardian geometry targets the exposed arm");
    check(prison::raycastGuardian(guardian, {-3, 1.5f, 0}, {1, 0, 0}, 1).part == -1,
          "terrain occlusion bounds guardian reach");
    check(!game.strikeGuardian(2).ok && guardian["health"] == 120, "armor rejects strikes");
    for (int i = 0; i < 40; ++i)
        check(game.strikeGuardian(4).ok, "first exposed arm accepts mining");
    check(prison::guardianWeakPart(guardian) == 5 && !game.strikeGuardian(4).ok,
          "weak spot moves after the first phase");
    game.tickGuardian(10);
    const auto saved = guardian;
    game.loadSnapshot(game.snapshot());
    check(game.state["guardian"] == saved, "guardian health and timers survive reload");
    for (int i = 0; i < 80; ++i)
        check(game.strikeGuardian(prison::guardianWeakPart(game.state["guardian"])).ok,
              "remaining guardian phases accept mining");
    check(game.state["points"].asDouble() == 3 && !game.strikeGuardian(3).ok &&
              game.state["guardian"]["defeated"] == 1,
          "guardian death awards points exactly once");
    check(!game.spawnGuardian({}), "guardian cooldown prevents instant respawn");
    game.loadSnapshot(game.snapshot());
    check(game.state["points"].asDouble() == 3, "guardian points persist");
    game.state["devmode"] = true;
    check(game.spawnGuardian({}, true) && game.tickGuardian(180), "guardian deadline expires");
    check(game.state["points"].asDouble() == 3, "expired encounter cannot award points");
}

void retiredContent(const prison::Data& data) {
    prison::Game game(data);
    auto old = game.snapshot();
    auto part = equipped("pick_part", "exp", .22, 4);
    part["slot"] = "head";
    part["uid"] = "stored-guardian-part";
    part["guardian_forged"] = true;
    part["specialization"] = "scholar";
    old["forge_slots"].append(part);
    old["progression"]["materials"]["guardian_core"] = 6;
    old["progression"]["materials"]["stone_shard"] = 11;
    old["progression"]["upgrades"]["crate_quality"] = 2;
    old["progression"]["upgrades"]["ore_density"] = 2;
    old["money"] = 0;
    old["points"] = 0;
    old["cleaner_settings"]["enabled"] = true;
    old["cleaner_settings"]["mode"] = "blacklist";
    old["cleaner_settings"]["default_action"] = "salvage";
    game.loadSnapshot(old);
    near(game.state["money"].asDouble(), 1e18 + 2e21, "removed crate upgrades refund money");
    check(game.state["points"].asDouble() == 8517, "materials and crate upgrade points convert once");
    check(!game.state.isMember("forge_slots") && !game.state["progression"].isMember("materials") &&
              !game.state["progression"]["upgrades"].isMember("crate_quality") &&
              game.state["progression"]["upgrades"]["ore_density"] == 2,
          "retired state is removed while other upgrades stay owned");
    int found = 0;
    for (const auto& row : game.state["inventory_slots"])
        found += row == part;
    check(found == 1, "forge returns preserve metadata and bypass filters");
    const auto migrated = game.snapshot();
    game.loadSnapshot(migrated);
    check(game.snapshot() == migrated, "loading again cannot repeat refunds or return items twice");
    check(!game.command("/forge").ok && !game.itemAction("forge").ok,
          "retired forging cannot be used through commands or actions");

    auto full = old;
    for (unsigned i = 1; i < full["inventory_slots"].size(); ++i) {
        auto item = equipped("pet", "sell", .1);
        item["uid"] = "inventory-" + std::to_string(i);
        full["inventory_slots"][i] = item;
    }
    game.loadSnapshot(full);
    check(game.state["vault_slots"][0] == part && !game.state.isMember("forge_slots"),
          "full inventory returns forge items to the vault");
    for (unsigned i = 0; i < full["vault_slots"].size(); ++i) {
        auto item = equipped("pet", "sell", .1);
        item["uid"] = "vault-" + std::to_string(i);
        full["vault_slots"][i] = item;
    }
    game.loadSnapshot(full);
    check(game.state["forge_slots"][0] == part, "full storage retains pending items");
    game.loadSnapshot(game.snapshot());
    check(game.state["forge_slots"][0] == part, "pending returns survive reload");
    game.state["inventory_slots"][8] = prison::Json();
    game.tick(1);
    check(game.state["inventory_slots"][8] == part && !game.state.isMember("forge_slots"),
          "making room returns pending items during play");
    game.state["equipped_parts"]["head"] = part;
    near(game.specializationModifiers()["xp"].asDouble(), 1.15, "owned part bonuses survive cleanup");

    auto legacy = prison::Game(data).snapshot();
    legacy["progression"]["contract"]["id"] = "caves";
    legacy["progression"]["contract"]["kind"] = "caves";
    legacy["progression"]["contract"]["region"] = "quarry";
    legacy["progression"]["contract"]["goal"] = 2;
    legacy["progression"]["contract"]["progress"] = 2;
    legacy["progression"]["contract"]["points"] = 8;
    legacy["progression"]["contract"]["materials"] = 3;
    game.loadSnapshot(legacy);
    check(game.state["points"].asDouble() == 11 && game.state["progression"]["contract"].isNull(),
          "completed retired contracts pay their accepted reward");
    game.loadSnapshot(game.snapshot());
    check(game.state["points"].asDouble() == 11, "retired contract rewards cannot repeat");
    legacy["progression"]["contract"]["progress"] = 1;
    game.loadSnapshot(legacy);
    check(game.state["points"].asDouble() == 0 && game.state["progression"]["contract"].isNull(),
          "unfinished retired contracts free the active slot");
}

void nativeEconomy(prison::Data data) {
    prison::Game game(data);
    for (double progress : {0.0, .1, .5, .99}) {
        double tokenPrice = 0;
        for (const auto& id : data.enchants["order"]) {
            const auto key = id.asString();
            const int maximum = data.enchants["defs"][key]["max"].asInt();
            if (maximum <= 0)
                continue;
            game.state["enchants"][key] = int(maximum * progress);
            if (game.enchantCurrency(key) == "tokens")
                tokenPrice = std::max(tokenPrice, game.enchantCost(key));
        }
        for (const auto& id : data.enchants["order"])
            if (game.enchantCurrency(id.asString()) != "tokens")
                check(game.enchantCost(id.asString()) > tokenPrice,
                      "gem levels cost more than token levels at comparable progress");
    }
    game.newWorld();
    auto oldSettings = game.snapshot();
    oldSettings["message_settings"].removeMember("visual_enchants");
    oldSettings["message_settings"]["visual_drill"] = true;
    game.loadSnapshot(oldSettings);
    check(game.state["message_settings"]["visual_enchants"] == true &&
              !game.state["message_settings"].isMember("visual_drill"),
          "individual visual settings migrate to one switch");
    game.state["message_settings"]["visual_enchants"] = false;
    game.loadSnapshot(game.snapshot());
    check(game.state["message_settings"]["visual_enchants"] == false, "visual off survives reload");
    game.state["pick_level"] = 5000;
    game.state["tokens"] = 1e20;
    check(!game.buyEnchant("drill").ok, "tokens cannot buy a ruby enchant");
    game.state["rubies"] = game.enchantCost("drill", 2);
    const auto tokens = game.state["tokens"];
    check(game.buyEnchant("drill", 0).ok && game.enchantLevel("drill") == 2,
          "ruby buy-max uses the ruby wallet");
    near(game.state["rubies"].asDouble(), 0, "ruby buy-max spends exact balance");
    check(game.state["tokens"] == tokens, "ruby purchase never spends tokens");
    game.state["sapphires"] = game.enchantCost("jackpot");
    check(game.buyEnchant("jackpot").ok && game.state["sapphires"].asDouble() == 0,
          "sapphire purchase charges sapphire wallet");
    game.state["pick_level"] = 39;
    game.state["rubies"] = 1000;
    check(!game.buyEnchant("drill").ok, "gem funding cannot bypass pick level");

    game.newWorld();
    game.state["pick_level"] = 150;
    for (const char* id : {"ruby_greed", "sapphire_greed"}) {
        game.state["enchants"][id] = 1;
        data.enchants["effects"][id]["chance_per_level"] = 1.0;
        data.enchants["effects"][id]["chance_cap"] = 1.0;
    }
    game.mineBlock("tier_a_smooth", false);
    check(game.state["rubies"].asDouble() == 1000000 && game.state["sapphires"].asDouble() == 1000000,
          "greed pays manual gems");
    const auto context = game.captureRewardContext();
    for (const char* source : {"explosive", "jackhammer", "veinminer", "drill"})
        game.mineBlock("tier_a_smooth", false, false, source, context);
    check(game.state["rubies"].asDouble() == 1000000 && game.state["sapphires"].asDouble() == 1000000,
          "proc blocks cannot mint gems");

    game.newWorld();
    auto robot = game.makeRobot("sell");
    robot["fuel"] = 2;
    game.state["robot_slots"][0] = robot;
    near(game.robotMultiplier(robot), 1.001, "robot starts at 0.1 percent");
    game.tick(120);
    check(game.state["robot_slots"][0]["fuel"] == 2, "idle time cannot spend robot fuel");
    near(game.consumeRobotFuel(3)["money"].asDouble(), 1 + .001 * 2 / 3,
         "partial batches fund only covered blocks");
    check(game.state["robot_slots"][0]["fuel"] == 0, "funded fuel spent once");
    near(game.consumeRobotFuel(1)["money"].asDouble(), 1, "empty robot gives no boost");
    robot["level"] = 4000;
    near(game.robotMultiplier(robot), 5, "robot multiplier cap");
    check(game.robotFuelCost(robot) == 4, "late robots use four fuel per block");
    game.state["robot_slots"][0] = robot;
    check(game.robotUpgradeQuote(0, 1).isMember("error"), "max robot cannot be upgraded");

    robot["level"] = 99;
    robot["fuel"] = 250;
    game.state["robot_slots"][0] = robot;
    const auto upgrade = game.robotUpgradeQuote(0, 2);
    near(upgrade["rubies"].asDouble(), 18125000000.0, "robot quote scales both crossed level prices");
    near(upgrade["sapphires"].asDouble(), 1250000000.0, "robot quote includes the first sapphire level");
    game.state["rubies"] = upgrade["rubies"];
    game.state["sapphires"] = upgrade["sapphires"].asDouble() - 1;
    const auto beforeUpgrade = game.snapshot();
    check(!game.itemAction("robot_upgrade", upgrade).ok && game.snapshot() == beforeUpgrade,
          "insufficient sapphire upgrade cannot spend rubies or change fuel");
    game.state["sapphires"] = upgrade["sapphires"];
    check(game.itemAction("robot_upgrade", upgrade).ok && game.state["robot_slots"][0]["level"] == 101 &&
              game.state["robot_slots"][0]["fuel"] == 250,
          "quoted gem upgrade applies once and preserves loaded fuel");

    game.newWorld();
    auto legacy = game.snapshot();
    legacy.removeMember("gem_currency_version");
    legacy["rubies"] = 7;
    legacy["sapphires"] = 3;
    legacy["tokens"] = 50;
    game.loadSnapshot(legacy);
    near(game.state["rubies"].asDouble(), 7000000, "legacy rubies change denomination");
    near(game.state["sapphires"].asDouble(), 3000000, "legacy sapphires change denomination");
    near(game.state["tokens"].asDouble(), 50, "gem migration cannot inflate tokens");
    game.loadSnapshot(game.snapshot());
    near(game.state["rubies"].asDouble(), 7000000, "gem migration only runs once");
    game.state["rubies"] = 1e20;
    game.loadSnapshot(game.snapshot());
    near(game.state["rubies"].asDouble(), 1e20, "large gem wallets survive reload");
    legacy = game.snapshot();
    legacy.removeMember("vault_page_size");
    legacy["vault_slots"].resize(54);
    legacy["vault_slots"][0] = robot;
    auto egg = equipped("pet_egg", "exp", 0);
    egg["tier_id"] = "rare";
    legacy["vault_slots"][27] = egg;
    legacy["vault_names"][0] = "Robots";
    legacy["vault_names"][1] = "Eggs";
    game.loadSnapshot(legacy);
    check(game.vaultPages() == 2 && game.state["vault_slots"].size() == 108,
          "legacy pages expand separately");
    check(game.vaultSlots(0)[0] == robot && game.vaultSlots(1)[0] == egg && game.vaultName(1) == "Eggs",
          "vault migration retains metadata and named page ownership");
    const auto expanded = game.snapshot();
    game.loadSnapshot(expanded);
    check(game.state["vault_slots"] == expanded["vault_slots"], "vault expansion is idempotent");
}

void endgame(prison::Data data) {
    for (const auto& id : data.systems["endgame"]) {
        data.enchants["effects"][id.asString()]["chance_cap"] = 1.0;
        data.enchants["effects"][id.asString()]["chance_per_level"] = 1.0;
    }
    prison::Game game(data);
    std::vector<prison::Proc> procs;
    game.state["enchants"]["hyper_surge"] = 1;
    game.state["pick_level"] = 999;
    game.rollEndgame(procs);
    near(game.endgameMultiplier("hyper_surge"), 1, "endgame pick-level gate");
    game.state["pick_level"] = 1000;
    game.rollEndgame(procs);
    near(game.endgameMultiplier("hyper_surge"), 1.5, "surge fixed income boost");
    game.tick(3);
    game.rollEndgame(procs);
    near(game.state["endgame"]["hyper_surge"]["remaining"].asDouble(), 17,
         "active proc cannot extend itself");
    const auto timers = game.state["endgame"];
    game.loadSnapshot(game.snapshot());
    check(game.state["endgame"] == timers, "remaining effect and cooldown survive reload");
    game.tick(17);
    near(game.endgameMultiplier("hyper_surge"), 1, "surge expires");
    game.rollEndgame(procs);
    near(game.endgameMultiplier("hyper_surge"), 1, "cooldown prevents retriggering");

    game.newWorld();
    game.state["pick_level"] = 3500;
    game.state["enchants"]["rewind"] = 1;
    const auto earned = game.mineBlock("tier_a_smooth", false);
    const auto blocks = game.state["blocks_mined"];
    const double before = game.state["money"].asDouble();
    game.rollEndgame(procs);
    near(game.state["money"].asDouble(), before + earned.money, "rewind repeats original income once");
    check(game.state["blocks_mined"] == blocks, "rewind does not duplicate block rewards");
    game.state["endgame"]["rewind"]["cooldown"] = 0;
    game.rollEndgame(procs);
    near(game.state["money"].asDouble(), before + earned.money, "rewind cannot repeat a replay");
    game.mineBlock("tier_a_smooth", false);
    game.tick(9);
    const auto expired = game.state["money"];
    game.rollEndgame(procs);
    check(game.state["money"] == expired, "rewind ignores expired income");

    game.newWorld();
    game.state["pick_level"] = 5000;
    game.state["enchants"]["energizer"] = 1;
    game.state["enchants"]["overdrive"] = 1;
    data.enchants["effects"]["energizer"]["activates"] = prison::Json(::Json::arrayValue);
    data.enchants["effects"]["energizer"]["activates"].append("overdrive");
    game.rollEndgame(procs);
    near(game.endgameMultiplier("overdrive"), 2, "energizer activates an eligible owned effect");
    game.tick(1);
    game.state["endgame"]["energizer"]["cooldown"] = 0;
    game.rollEndgame(procs);
    near(game.state["endgame"]["overdrive"]["remaining"].asDouble(), 29,
         "energizer respects target cooldown");

    game.newWorld();
    game.state["pick_level"] = 5000;
    game.state["enchants"]["credit_finder"] = 1;
    game.mineBlock("tier_a_smooth", false, false, "drill");
    check(game.state["points"].asDouble() == 0, "drill cannot roll credit finder");
    game.rollEndgame(procs);
    check(game.state["points"].asDouble() == 1, "credit finder awards an existing shop point");
}
} // namespace

int main(int argc, char** argv) {
    try {
        prison::Data data;
        data.load(argc > 1 ? argv[1] : "..");
        nativeContent(data);
        retiredContent(data);
        nativeEconomy(data);
        tutorial(data);
        endgame(data);
        prison::Game game(data);
        game.random.seed(12345);
        check(!game.state["devmode"].asBool(), "old and ordinary worlds must not enable developer mode");
        check(game.state["inventory_slots"].size() == 36 &&
                  game.state["inventory_slots"][0]["id"] == "pickaxe",
              "starter inventory");
        check(game.state["vault_slots"].size() == prison::VaultPageSize, "vault size");
        near(game.pickXpNeeded(1), 180, "source initial pick XP");
        near(game.pickXpNeeded(100), 13120, "source pick XP curve");
        const auto first = game.mineBlock("tier_a_smooth", false);
        near(first.money, 95, "source initial sell reward");
        near(first.xp, .8143, "experienced zero fractional XP");
        near(game.state["pick_xp"].asDouble(), .8143, "fractional XP is retained");
        check(first.procs.empty(), "disabled nested procs");
        game.state["autominer_active"] = true;
        const auto blocked = game.mineBlock("tier_a_smooth");
        check(!blocked.ok && game.state["blocks_mined"].asDouble() == 1, "manual mining while automining");
        check(game.mineBlock("tier_a_smooth", false, false).ok, "automatic mining allowed");
        game.newWorld();

        data.balance["token_drop_min"] = 100;
        data.balance["token_drop_max"] = 100;
        const auto ore = game.mineBlock("tier_a_ore", false);
        near(ore.money, 0, "token ores must not pay sell money");
        near(ore.tokens, 239, "raw token greed roll before token factor and pace");
        game.state["enchants"]["experienced"] = 1000;
        const auto experience = game.mineBlock("tier_a_smooth", false);
        near(experience.xp, 6.14 * 1.1975, "source experienced piecewise curve");
        const auto jackhammer = game.mineBlock("tier_a_smooth", false, false, "jackhammer");
        near(jackhammer.xp, 0, "jackhammer must not produce pick XP");
        const auto veinminer = game.mineBlock("tier_a_smooth", false, false, "veinminer");
        near(veinminer.xp, 0, "veinminer must not produce pick XP");

        game.newWorld();
        near(game.rankCost(1), 1500, "initial B rank cost");
        near(game.rankCost(25), 7372800000.0, "source late-rank cost floor");
        game.state["money"] = 5400;
        const auto ranks = game.rankUp(true);
        check(ranks.ok && ranks.resetMine && game.state["rank_name"] == "C", "rankmax one resulting refill");
        near(game.state["money"].asDouble(), 0, "rankmax exact expenditure");
        game.state["money"] = 1e20;
        check(game.rankUp(true).ok, "rankmax final rank");
        const auto unconfirmed = game.prestige();
        check(unconfirmed.ok && unconfirmed.menu == "prestige_confirm" &&
                  game.state["total_prestige"].asDouble() == 0,
              "prestige confirmation no mutation");
        check(game.prestige(true).ok, "confirmed prestige");
        near(game.state["money"].asDouble(), 0, "prestige clears money");
        near(game.state["tokens"].asDouble(), 100000000, "first prestige token milestone");
        near(game.rankCost(1), 2923, "lifetime prestige rank price growth");
        game.state["rank_index"] = 25;
        game.state["prestige_level"] = 100;
        game.state["total_prestige"] = 100;
        game.state["money"] = 1e30;
        check(game.rebirth(true).ok && game.state["rebirth"].asDouble() == 1 &&
                  game.state["prestige_level"].asInt() == 0,
              "prestige cycle automatically rebirths");
        game.state["total_prestige"] = 9e15;
        check(std::isfinite(game.rankCost(25)) && game.rankCost(25) == 9e33,
              "extreme progression cost saturates before pow overflow");

        game.newWorld();
        check(!game.buyEnchant("experienced").ok, "enchant unlock gate");
        game.state["pick_level"] = 5000;
        game.state["tokens"] = 9e33;
        game.state["rubies"] = 9e15;
        game.state["sapphires"] = 9e15;
        game.state["enchants"]["fortune"] = 249;
        const double ordinary = game.enchantCost("fortune");
        game.state["enchants"]["fortune"] = 250;
        check(game.enchantCost("fortune") > ordinary * 99, "fortune late price discontinuity");
        check(game.buyEnchant("fortune", 20000).ok && game.enchantLevel("fortune") == 10000,
              "Fortune 10000 cap");
        check(!game.buyEnchant("fortune").ok, "cannot exceed enchant maximum");
        for (const auto& id : data.enchants["order"]) {
            if (id.asString() == "fortune")
                continue;
            const auto wallet = game.enchantCurrency(id.asString());
            game.state[wallet] = game.enchantCost(id.asString());
            check(game.buyEnchant(id.asString()).ok, "catalogue enchant purchase");
        }
        game.newWorld();
        game.state["tokens"] = game.enchantCost("efficiency", 3);
        check(game.buyEnchant("efficiency", 0).ok && game.enchantLevel("efficiency") == 3,
              "Buy Max exact prefix affordability");

        game.newWorld();
        game.state["pick_level"] = 19;
        game.state["equipped_crystals"].append(equipped("crystal", "sell", .5));
        near(game.boost("money"), 1, "locked crystal must not boost");
        game.state["pick_level"] = 20;
        check(game.crystalSlots() == 1, "first crystal unlock");
        near(game.boost("money"), 1.5, "source fractional crystal boost");
        game.state["equipped_parts"]["head"] = equipped("pick_part", "cash boost", .2);
        game.state["parts_prestige"] = 3;
        near(game.boost("money"), 2, "parts plus crystal additive source bonus");
        game.state["pick_level"] = 5000;
        check(game.crystalSlots() == 14, "all fourteen crystal slots unlock");
        game.state["pet_data"]["active_pet"] = equipped("pet", "money", 0);
        game.state["pet_data"]["active_pet"]["base_boost_pct"] = 10;
        game.state["pet_data"]["active_pet"]["level"] = 1;
        near(game.boost("money"), 2.2, "pet percent is separate multiplicative bonus");
        prison::Json timed;
        timed["amount"] = 3;
        timed["remaining"] = 2;
        game.state["boosts"]["money"].append(timed);
        timed["amount"] = 2;
        timed["remaining"] = 20;
        game.state["boosts"]["money"].append(timed);
        near(game.boost("money"), 6.6, "highest timed multiplier only");
        near(game.activeMultiplier("sell").amount, 3, "timer shows sell multi without equipment bonus");
        near(game.activeMultiplier("money").remaining, 2, "timer follows strongest active multi");
        game.tick(3);
        near(game.boost("money"), 4.4, "expired timed boost removed");
        near(game.activeMultiplier("money").remaining, 17, "weaker multi keeps its own countdown");
        for (const char* slot : {"head", "binding", "rod"})
            game.state["equipped_parts"][slot] = equipped("pick_part", "sell", .1, 4);
        check(game.prestigeParts().ok && game.state["parts_prestige"].asInt() == 4,
              "parts prestige uses its own tier");
        check(game.state["equipped_parts"]["head"].isNull(), "parts prestige consumes equipment");

        game.newWorld();
        game.state["blocks_mined"] = 49;
        game.mineBlock("tier_a_smooth", false);
        near(game.state["money"].asDouble(), 5095, "50 block milestone money");
        near(game.state["tokens"].asDouble(), 2500, "50 block milestone tokens");
        game.tick(60);
        near(game.state["mining_summary"]["money"].asDouble(), 5095, "one minute summary includes milestone");
        game.state["blocks_mined"] = 9999;
        game.mineBlock("tier_a_smooth", false);
        near(game.state["points"].asDouble(), 1, "mined block point boundary");
        game.state["pick_xp"] = 1e30;
        const int before = game.state["pick_level"].asInt();
        game.tick(.01);
        check(game.state["pick_level"].asInt() - before == 32, "XP level catchup bounded per frame");

        game.newWorld();
        game.state["parts_prestige"] = 3;
        data.enchants["effects"]["partfinder"]["blocks_per_drop_base"] = 1;
        data.enchants["effects"]["partfinder"]["blocks_per_drop_min"] = 1;
        game.mineBlock("tier_a_smooth", false);
        bool tier4 = false;
        for (const auto& row : game.state["inventory_slots"])
            if (row.isObject() && row["id"] == "pick_part")
                tier4 = row["tier"].asInt() == 4;
        check(tier4, "parts finder drops current parts prestige tier");

        game.newWorld();
        game.state["money"] = -1;
        const auto beforeInvalid = game.snapshot();
        check(!game.rankUp().ok && game.snapshot() == beforeInvalid,
              "invalid rank transaction leaves state untouched");
        game.state["tokens"] = std::numeric_limits<double>::infinity();
        check(!game.buyEnchant("efficiency").ok, "nonfinite token purchase rejected");

        game.newWorld();
        game.state["pet_data"]["hatchery"].append(equipped("pet", "sell", .1));
        game.state["pet_data"]["hatchery"][0]["required"] = 25000;
        game.state["pet_data"]["hatchery"][0]["progress"] = 0;
        game.state["pet_data"]["hatchery"][0]["ready"] = false;
        for (int n = 0; n < 10; ++n)
            game.mineBlock("tier_a_smooth", false, false, "jackhammer");
        near(game.state["pet_data"]["hatchery"][0]["progress"].asDouble(), 0,
             "proc hatchery progress waits for completion");
        game.finishProc(10, 1);
        near(game.state["pet_data"]["hatchery"][0]["progress"].asDouble(), 11,
             "proc awards virtual plus compressed hatchery progress");
        near(game.state["blocks_mined"].asDouble(), 10, "compressed terrain does not double-pay block count");

        prison::Json broken;
        broken["devmode"] = "yes";
        broken["money"] = -12;
        broken["tokens"] = std::numeric_limits<double>::infinity();
        broken["pick_level"] = "bad";
        broken["inventory_slots"] = "bad";
        broken["equipped_parts"] = 8;
        broken["boosts"] = false;
        broken["pet_data"] = "bad";
        broken["cleaner_settings"] = 12;
        broken["mine_state"]["preserve_me"] = 1;
        broken["future_extension"]["value"] = 7;
        broken["parts_prestige"] = 1e100;
        game.loadSnapshot(broken);
        check(!game.state["devmode"].asBool(), "string devmode rejected");
        near(game.state["money"].asDouble(), 0, "negative save currency clamped");
        near(game.state["tokens"].asDouble(), 0, "nonfinite save currency rejected");
        check(game.state["pick_level"].asInt() == 1 && game.state["inventory_slots"].size() == 36,
              "malformed nested save normalized");
        check(game.snapshot()["future_extension"]["value"] == 7 &&
                  game.snapshot()["mine_state"]["preserve_me"] == 1,
              "unknown metadata and mine excavation preserved");
        std::cout << "game progression, rewards, source formulas, and snapshot checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "game_test: " << error.what() << '\n';
        return 1;
    }
}
