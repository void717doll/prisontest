#include "prison/game.hpp"
#include <iostream>
#include <stdexcept>
using Value = prison::Json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Value item(const char* id, int count = 1) {
    Value value;
    value["id"] = id;
    value["count"] = count;
    return value;
}
Value args(int index) {
    Value value;
    value["index"] = index;
    return value;
}
int find(const prison::Game& game, const char* family) {
    for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i)
        if (game.state["inventory_slots"][i].isObject() && game.state["inventory_slots"][i]["id"] == family)
            return int(i);
    return -1;
}
int total(const Value& slots, const char* family) {
    int result = 0;
    for (const auto& row : slots)
        if (row.isObject() && row["id"] == family)
            result += row.get("count", 1).asInt();
    return result;
}
void clear(prison::Game& game) {
    game.newWorld(true);
    for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i)
        game.state["inventory_slots"][i] = Value();
}
void full(prison::Game& game) {
    for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i) {
        Value value = item("pet");
        value["uid"] = std::to_string(i);
        game.state["inventory_slots"][i] = value;
    }
}
void filterRouting(const prison::Data& data) {
    prison::Game game(data);
    clear(game);
    game.state["vault_slots"].resize(prison::VaultPageSize * 2);
    Value settings, rule;
    settings["enabled"] = true;
    settings["vault"] = 2;
    rule["pattern"] = "prisontest:*";
    rule["action"] = "salvage";
    settings["rules"].append(rule);
    rule["pattern"] = "crystal";
    rule["action"] = "vault";
    settings["rules"].append(rule);
    check(game.itemAction("filter", settings).ok, "save filter destination");
    Value crystal = item("crystal", 4);
    crystal["tier"] = 2;
    check(game.giveItem(crystal, true), "exact rule overrides family");
    check(game.state["vault_slots"][0].isNull() &&
              game.state["vault_slots"][prison::VaultPageSize] == crystal,
          "filter must use only its selected vault page");
    game.loadSnapshot(game.snapshot());
    check(game.state["cleaner_settings"]["vault"] == 2, "filter destination survives reload");
    for (int i = prison::VaultPageSize; i < prison::VaultPageSize * 2; ++i)
        game.state["vault_slots"][i] = item("pet");
    check(game.giveItem(crystal, true), "full destination falls back to inventory");
    check(game.state["vault_slots"][0].isNull() && total(game.state["inventory_slots"], "crystal") == 4,
          "filter must not spill into another vault page");
    full(game);
    const auto before = game.snapshot();
    check(!game.giveItem(crystal, true) && game.snapshot() == before, "failed routing must be atomic");
    rule["action"] = "keep";
    check(game.itemAction("filter_rule", rule).ok && game.state["cleaner_settings"]["rules"].size() == 2,
          "editing an alias must replace its existing rule");
    rule["pattern"] = "*:crystal";
    check(!game.itemAction("filter_rule", rule).ok, "invalid wildcard rejected");
    check(game.filterPattern(item("robot_fuel"), true) == "prisontest_robots:*", "robot family supported");
    clear(game);
    settings["mode"] = "blacklist";
    settings["default_action"] = "apply";
    check(game.itemAction("filter", settings).ok, "blacklist accepts default action");
    Value time = item("automine_time", 2);
    time["duration"] = 100;
    check(game.giveItem(time, true) && game.state["autominer_time"].asDouble() == 200,
          "unlisted automatic consumables apply");
    check(game.giveItem(crystal, true) && total(game.state["inventory_slots"], "crystal") == 4,
          "blacklisted exact and family matches remain in inventory");
    check(game.giveItem(time) && game.state["autominer_time"].asDouble() == 200,
          "manual consumables must bypass automatic application");
}
} // namespace
int main(int argc, char** argv) {
    try {
        prison::Data data;
        data.load(argc > 1 ? argv[1] : "..");
        filterRouting(data);
        prison::Game game(data);
        clear(game);
        game.random.seed(1);
        Value crystal = item("crystal", 65);
        crystal["tier"] = 3;
        check(game.giveItem(crystal), "grant stack split");
        check(game.state["inventory_slots"][1]["count"].asInt() == 64 &&
                  game.state["inventory_slots"][2]["count"].asInt() == 1,
              "source crystal limit64");
        Value before = game.state["inventory_slots"];
        check(!game.transfer("inventory", -1, "inventory", 0).ok, "negative index rejected");
        check(!game.transfer("inventory", 999, "inventory", 0).ok, "large index rejected");
        check(game.state["inventory_slots"] == before, "invalid move rollback");
        check(game.itemAction("identify", args(1)).ok, "identify one stacked crystal");
        check(total(game.state["inventory_slots"], "crystal") == 65, "identification conserves count");
        int identified = -1;
        for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i)
            if (game.state["inventory_slots"][i].get("identified", false).asBool())
                identified = int(i);
        check(identified > 0, "identified item retained");
        Value rolled = game.state["inventory_slots"][identified];
        game.state["pick_level"] = 10000;
        check(game.transfer("inventory", identified, "crystals", 0).ok, "equip identified crystal");
        check(game.transfer("crystals", 0, "inventory").ok, "return crystal");
        check(game.state["inventory_slots"][identified] == rolled, "crystal metadata preserved");
        clear(game);
        Value dust = item("crystal_dust", 8);
        dust["tier"] = 2;
        check(game.giveItem(dust), "grant dust");
        Value craft = args(1);
        craft["all"] = true;
        check(game.itemAction("crystal_craft", craft).ok, "bulk craft");
        check(total(game.state["inventory_slots"], "crystal_dust") == 0 &&
                  total(game.state["inventory_slots"], "crystal") == 2,
              "craft updates instantly and conserves inputs");
        clear(game);
        full(game);
        game.state["inventory_slots"][1] = dust;
        before = game.state["inventory_slots"];
        check(!game.itemAction("crystal_craft", args(1)).ok, "craft requires free slot");
        check(game.state["inventory_slots"] == before, "failed crafting atomic");
        clear(game);
        check(game.command("/givepouch small").ok, "give pouch");
        check(game.command("/givefood 8").ok, "give food");
        int pouch = find(game, "pouch"), food = find(game, "pet_food");
        const std::string bag = "pouch:" + std::to_string(pouch);
        check(game.transfer("inventory", food, bag, 0).ok, "put food into pouch");
        check(game.transfer(bag, 0, "inventory", food).ok, "return food from pouch");
        check(game.state["inventory_slots"][pouch]["items"][0].isNull(),
              "returned pouch item removed exactly once");
        check(game.state["inventory_slots"][food]["count"].asInt() == 8, "pouch return count");
        check(!game.transfer("inventory", pouch, bag, 0).ok, "reject nesting pouch into itself");
        Value filter;
        filter["enabled"] = true;
        filter["mode"] = "whitelist";
        filter["default_action"] = "salvage";
        Value rule;
        rule["pattern"] = "prisontest_pets:*";
        rule["action"] = "vault";
        filter["rules"].append(rule);
        check(game.itemAction("filter", filter).ok, "enable pet filter");
        Value pet = item("pet");
        pet["tier_id"] = "rare";
        pet["uid"] = "unique-pet";
        pet["mood"] = "Gentle";
        pet["pet_type"] = "Sheep";
        pet["custom_field"] = "retained";
        pet["level"] = 5;
        check(game.giveItem(pet, true), "automatic pet routed");
        check(game.state["vault_slots"][0] == pet, "automatic filter vault");
        check(game.transfer("vault", 0, "inventory").ok, "manual vault return bypasses filter");
        int petSlot = find(game, "pet");
        check(petSlot > 0 && game.state["vault_slots"][0].isNull(), "pet returned to inventory");
        check(game.transfer("inventory", petSlot, "pet", 0).ok, "equip pet");
        check(game.transfer("pet", 0, "inventory", petSlot).ok, "unequip pet bypasses filter");
        check(game.state["inventory_slots"][petSlot] == pet, "pet metadata survives moves");
        check(game.transfer("inventory", petSlot, "pet", 0).ok, "reequip pet");
        full(game);
        before = game.state["inventory_slots"];
        check(!game.transfer("pet", 0, "inventory").ok, "full inventory pet return rejected");
        check(game.state["pet_data"]["active_pet"] == pet && game.state["inventory_slots"] == before,
              "failed pet return loses nothing");
        clear(game);
        check(game.command("/giveegg 2 common").ok, "grant eggs");
        check(game.useItem(1).ok, "hatch first egg");
        check(game.state["pet_data"]["hatchery"].size() == 1, "hatchery appended");
        game.tickItems(0, 25000);
        check(game.state["pet_data"]["hatchery"][0]["ready"].asBool(), "hatchery progress source threshold");
        check(game.transfer("hatchery", 0, "inventory").ok, "claim pet");
        check(game.state["pet_data"]["hatchery"].empty() && find(game, "pet") > 0,
              "claimed hatchery pet retained");
        check(game.useItem(1).ok, "hatch second egg");
        check(game.transfer("hatchery", 0, "inventory").ok, "return unfinished egg");
        check(find(game, "pet_egg") > 0, "unfinished hatch returns egg");
        clear(game);
        for (int i = 0; i < 5; ++i)
            check(game.command("/givepet mythical").ok, "grant merge pets");
        check(game.itemAction("merge_pets").ok, "merge five mythical pets");
        check(total(game.state["inventory_slots"], "pet") == 1 &&
                  game.state["inventory_slots"][find(game, "pet")]["tier_id"] == "overpowered",
              "merge output correct");
        clear(game);
        check(game.command("/givemultiplier 2 1h 4").ok, "grant matching multipliers");
        check(game.itemAction("combinemulti", args(1)).ok, "combine multipliers");
        check(game.state["inventory_slots"][1]["amount"].asDouble() == 4 &&
                  game.state["inventory_slots"][1]["duration"].asDouble() == 10800,
              "combined amount and duration source parity");
        check(game.useItem(1).ok, "activate multiplier");
        check(game.itemAction("endmulti").ok, "return timed multiplier");
        check(game.state["inventory_slots"][1]["duration"].asDouble() == 10800, "return exact duration");
        game.state["boosts"]["money"].append(2.0);
        check(game.useItem(1).ok, "legacy numeric boost does not crash activation");
        check(game.itemAction("endmulti").ok, "legacy numeric boost does not crash return");
        clear(game);
        check(game.command("/givemultiplier 2 1h 4").ok, "grant quoted multiplier recipe");
        auto quote = game.multiplierRecipes()[0];
        game.state["inventory_slots"][1]["count"] = 5;
        before = game.state;
        check(!game.itemAction("combinemulti", quote).ok && game.state == before,
              "changed recipe cannot consume an unreviewed stack");
        check(game.itemAction("combinemulti", game.multiplierRecipes()[0]).ok,
              "refreshed recipe combines the matching four");
        check(total(game.state["inventory_slots"], "multiplier") == 2,
              "combine preserves the unmatched remainder");
        clear(game);
        check(game.command("/givemultiplier 2 1h 2").ok && game.command("/givemultiplier 2 30m 2").ok,
              "grant differing multiplier durations");
        before = game.state;
        check(game.multiplierRecipes().size() == 2 && !game.itemAction("combinemulti", args(1)).ok &&
                  game.state == before,
              "different durations stay separate and cannot combine");
        clear(game);
        full(game);
        auto multi = item("multiplier", 8);
        multi["amount"] = 20;
        multi["duration"] = 60;
        multi["boost_type"] = "sell";
        game.state["inventory_slots"][1] = multi;
        before = game.state;
        check(!game.itemAction("combinemulti", game.multiplierRecipes()[0]).ok && game.state == before,
              "full inventory combine is atomic");
        game.state["inventory_slots"][2] = Value();
        check(game.itemAction("combinemulti", game.multiplierRecipes()[0]).ok &&
                  game.state["inventory_slots"][2]["amount"].asDouble() == 25,
              "combined strength caps at twenty-five");
        clear(game);
        check(game.command("/givemultiplier 2 1h 2").ok, "grant stack for one-at-a-time activation");
        check(game.useItem(1).ok && game.state["inventory_slots"][1]["count"].asInt() == 1 &&
                  game.activeMultiplier("money").remaining == 3600,
              "activating a sell multi consumes only one item");
        game.tick(.25);
        check(game.useItem(1).ok && game.activeMultiplier("money").remaining == 7199.75,
              "equal sell multipliers extend remaining time");
        check(game.itemAction("endmulti").ok &&
                  game.state["inventory_slots"][1]["duration"].asDouble() == 7199.75,
              "returning a multiplier preserves fractional time without gaining duration");
        clear(game);
        game.state["points"] = 15;
        Value buy;
        buy["key"] = "rename_token";
        check(game.itemAction("buy_point", buy).ok, "point purchase");
        check(game.state["points"].asDouble() == 0, "point price");
        check(game.command("/rename").ok, "begin rename");
        Value rename;
        rename["text"] = "&#22aa33&l";
        check(!game.itemAction("rename_preview", rename).ok, "formatting-only name must be rejected");
        rename["text"] = "&l" + std::string(41, 'x');
        check(!game.itemAction("rename_preview", rename).ok, "visible name limit still applies");
        const std::string name = "chappellroan is mommy";
        std::string gradient;
        for (std::size_t i = 0; i < name.size(); ++i)
            gradient += std::string(i % 2 ? "&#448367&l" : "&#005155&l") + name[i];
        rename["text"] = gradient;
        check(game.itemAction("rename_preview", rename).ok, "rename color syntax preview");
        check(find(game, "rename_token") >= 0, "preview cannot consume rename token");
        check(game.itemAction("rename", rename).ok, "confirmed rename");
        check(game.itemName(game.state["inventory_slots"][0]) == name,
              "plain item name strips gradient and bold codes");
        game.loadSnapshot(game.snapshot());
        check(game.state["pick_custom_name"] == gradient, "saved name must retain gradient formatting");
        check(find(game, "rename_token") == -1, "rename consumes one token");
        clear(game);
        game.state["money"] = 1e20;
        check(game.itemAction("autominer_upgrade").ok, "paid autominer upgrade");
        check(game.state["autominer_unlocked_bps"].asInt() == 4, "autominer unlocked speed");
        check(game.command("/givetime 1h30m 2").ok, "duration command parses compounds");
        check(game.useItem(1).ok, "use one time item");
        check(game.state["autominer_time"].asDouble() == 5400 &&
                  game.state["inventory_slots"][1]["count"].asInt() == 1,
              "time item consumes one");
        check(game.command("/am start").ok, "autominer alias starts");
        check(!game.mineBlock("tier_a_smooth", false, true).ok, "manual mining rejected in autominer");
        game.state["devmode"] = false;
        before = game.state;
        check(!game.command("/givecrystal 10").ok, "grants devmode gated");
        check(!game.command("/test max").ok, "test presets devmode gated");
        check(game.state == before, "denied grants leave state");
        check(!game.command("/prestigemax").ok, "removed command remains removed");
        check(game.command("/bin").menu == "salvage" && game.command("/trash").menu == "salvage",
              "salvage aliases");
        check(game.command("/enchantcolor off").ok && !game.state["enchant_colors_enabled"].asBool(),
              "enchantcolor off");
        check(!game.command("/enchantcolor \\000000 #ffffff").ok, "bad color rejected");
        clear(game);
        check(game.command("/givecrystal 3 1").ok, "grant held crystal");
        const Value unidentified = game.state["inventory_slots"][1];
        check(game.activateHeld(1, false).menu == "confirm:crystal_dust:1",
              "held crystal right click quotes dust");
        check(game.state["inventory_slots"][1] == unidentified, "opening dust confirmation is nonmutating");
        check(game.activateHeld(1, true).ok && game.state["inventory_slots"][1]["identified"].asBool(),
              "held crystal left click identifies");
        check(game.activateHeld(0, false).menu == "pick", "held pickaxe secondary opens menu");
        check(game.command("/givepet common").ok, "grant held pet");
        petSlot = find(game, "pet");
        check(game.activateHeld(petSlot, false).menu == "confirm:pet_food:" + std::to_string(petSlot),
              "held pet secondary opens food confirmation");
        check(game.state["pet_data"]["active_pet"].isNull(), "held pet use does not equip");
        check(game.useItem(petSlot).ok && game.state["pet_data"]["active_pet"].isObject(),
              "inventory pet use equips");
        clear(game);
        check(game.command("/givexp 1t 1").ok, "grant large liquid EXP");
        check(game.itemAction("xp", args(1)).ok, "apply liquid EXP");
        check(game.state["pick_level"].asInt() == 33 && game.state["pick_xp"].asDouble() > 0,
              "large EXP respects source bounded levels and carries remainder");
        clear(game);
        Value customCrystal;
        customCrystal["id"] = "crystal";
        customCrystal["tier"] = "7";
        customCrystal["count"] = "2";
        customCrystal["identified"] = true;
        customCrystal["boost_type"] = "EXP Boost";
        customCrystal["percent"] = "12.7";
        check(game.giveTestItem(customCrystal).ok, "field grant accepts validated numeric strings");
        check(game.state["inventory_slots"][1]["tier"].asInt() == 7 &&
                  game.state["inventory_slots"][1]["boost_type"] == "exp" &&
                  std::abs(game.state["inventory_slots"][1]["boost"].asDouble() - .13) < 1e-9 &&
                  game.state["inventory_slots"][1]["uid"] != game.state["inventory_slots"][2]["uid"],
              "crystal field grant retains tier, chosen boost, source precision and unique metadata");
        Value customPart;
        customPart["id"] = "pick_part";
        customPart["tier"] = 4;
        customPart["identified"] = true;
        customPart["slot"] = "binding";
        customPart["boost_type"] = "token";
        customPart["percent"] = 12.34;
        check(game.giveTestItem(customPart).ok, "grant customized part");
        int customizedPart = find(game, "pick_part");
        check(game.state["inventory_slots"][customizedPart]["slot"] == "binding" &&
                  std::abs(game.state["inventory_slots"][customizedPart]["boost"].asDouble() - .123) < 1e-9,
              "part field grant uses three decimal fractional boost precision");
        Value customPet;
        customPet["id"] = "pet";
        customPet["rarity"] = "rare";
        customPet["pet_type"] = "sheep";
        customPet["level"] = "42";
        customPet["boost_type"] = "money";
        customPet["percent"] = "127.55";
        check(game.giveTestItem(customPet).ok, "grant fully customized pet");
        petSlot = find(game, "pet");
        check(game.state["inventory_slots"][petSlot]["tier_id"] == "rare" &&
                  game.state["inventory_slots"][petSlot]["pet_type"] == "Sheep" &&
                  game.state["inventory_slots"][petSlot]["level"].asInt() == 42 &&
                  game.state["inventory_slots"][petSlot]["boost_type"] == "sell" &&
                  game.state["inventory_slots"][petSlot]["base_boost_pct"].asDouble() == 127.55,
              "pet grant preserves every selected field");
        Value book;
        book["id"] = "enchant_book";
        book["enchant"] = "fortune";
        book["amount"] = "23";
        book["count"] = "2";
        check(game.giveTestItem(book).ok, "grant book levels and count independently");
        const int bookSlot = find(game, "enchant_book");
        check(game.state["inventory_slots"][bookSlot]["amount"].asInt() == 23 &&
                  game.state["inventory_slots"][bookSlot]["count"].asInt() == 2,
              "book level amount is not confused with item count");
        Value multiplier;
        multiplier["id"] = "multiplier";
        multiplier["boost_type"] = "token";
        multiplier["amount"] = "7.25";
        multiplier["duration"] = "1800";
        check(game.giveTestItem(multiplier).ok, "grant customized multiplier");
        const int multiSlot = find(game, "multiplier");
        check(game.state["inventory_slots"][multiSlot]["amount"].asDouble() == 7.25 &&
                  game.state["inventory_slots"][multiSlot]["boost_type"] == "token" &&
                  game.state["inventory_slots"][multiSlot]["duration"].asInt() == 1800,
              "multiplier options retained");
        Value customPouch;
        customPouch["id"] = "pouch";
        customPouch["size"] = "large";
        customPouch["count"] = 2;
        check(game.giveTestItem(customPouch).ok, "grant multiple unique pouches");
        check(game.state["inventory_slots"][find(game, "pouch")]["slots"].asInt() == 54,
              "selected pouch size retained");
        auto rejectGrant = [&](Value fields, const char* reason) {
            const Value previous = game.state;
            const auto previousRandom = game.random;
            check(!game.giveTestItem(fields).ok, reason);
            check(game.state == previous && game.random == previousRandom,
                  "invalid field grant mutates nothing");
        };
        Value invalid = customCrystal;
        invalid["count"] = 100;
        rejectGrant(invalid, "count above99 rejected");
        invalid["count"] = 0;
        rejectGrant(invalid, "zero count rejected");
        invalid["count"] = "1.5";
        rejectGrant(invalid, "fractional count rejected");
        invalid["count"] = "nan";
        rejectGrant(invalid, "nonfinite count rejected");
        invalid = customCrystal;
        invalid["tier"] = 1001;
        rejectGrant(invalid, "crystal tier bounds");
        invalid = customCrystal;
        invalid["identified"] = "true";
        rejectGrant(invalid, "identified requires actual boolean");
        invalid = customCrystal;
        invalid["percent"] = "inf";
        rejectGrant(invalid, "nonfinite boost rejected");
        invalid["percent"] = 1000001;
        rejectGrant(invalid, "boost upper bound");
        invalid = customPart;
        invalid["slot"] = "blade";
        rejectGrant(invalid, "part slot rejected");
        invalid = customPet;
        invalid["level"] = 76;
        rejectGrant(invalid, "pet level bound");
        invalid = customPet;
        invalid["pet_type"] = "Unknown Animal";
        rejectGrant(invalid, "unknown pet type");
        invalid = item("pet_egg");
        invalid["rarity"] = "overpowered";
        rejectGrant(invalid, "merge-only egg denied");
        invalid = book;
        invalid["amount"] = 10001;
        rejectGrant(invalid, "book levels bounded");
        invalid = book;
        invalid["enchant"] = "invalid";
        rejectGrant(invalid, "unknown enchant denied");
        invalid = multiplier;
        invalid["amount"] = 101;
        rejectGrant(invalid, "multiplier amount bound");
        invalid = multiplier;
        invalid["duration"] = 31536001;
        rejectGrant(invalid, "multiplier duration bound");
        invalid = item("minebomb");
        invalid["tier"] = 21;
        rejectGrant(invalid, "bomb tier bound");
        invalid = customPouch;
        invalid["size"] = "infinite";
        rejectGrant(invalid, "unknown pouch size");
        invalid = customCrystal;
        invalid["typo_option"] = true;
        rejectGrant(invalid, "unknown setting denied");
        rejectGrant(Value(), "nonobject grant denied");
        clear(game);
        full(game);
        game.state["inventory_slots"][34] = Value();
        game.state["inventory_slots"][35] = Value();
        customPet["count"] = 3;
        rejectGrant(customPet, "multi-item capacity failure rolls back earlier inserted pets");
        clear(game);
        Value pickFields;
        pickFields["name"] = " Custom Tool ";
        pickFields["level"] = "500";
        pickFields["prestige"] = "8";
        pickFields["enchant"] = "fortune";
        pickFields["enchant_level"] = "432";
        check(game.customizeTestPick(pickFields).ok, "customize combined pickaxe fields");
        check(game.state["pick_custom_name"] == "Custom Tool" && game.state["pick_level"].asInt() == 500 &&
                  game.state["parts_prestige"].asInt() == 7 &&
                  game.state["enchants"]["fortune"].asInt() == 432,
              "pickaxe selected fields applied exactly");
        Value badPick = pickFields;
        badPick["enchant_level"] = 10001;
        before = game.state;
        check(!game.customizeTestPick(badPick).ok && game.state == before,
              "invalid combined pick fields are atomic");
        check(!game.customizeTestPick(Value()).ok && game.state == before, "empty pick fields denied");
        const char* partSlots[] = {"head", "binding", "rod"};
        for (const char* slot : partSlots) {
            Value part = item("pick_part");
            part["tier"] = 8;
            part["identified"] = true;
            part["slot"] = slot;
            part["uid"] = slot;
            game.state["equipped_parts"][slot] = part;
        }
        full(game);
        game.state["inventory_slots"][34] = Value();
        game.state["inventory_slots"][35] = Value();
        Value downgrade;
        downgrade["name"] = "Do not apply yet";
        downgrade["level"] = 1;
        downgrade["prestige"] = 1;
        before = game.state;
        check(!game.customizeTestPick(downgrade).ok && game.state == before,
              "lowering prestige rolls back returned parts and all requested settings if bag is full");
        game.state["inventory_slots"][33] = Value();
        check(game.customizeTestPick(downgrade).ok, "lower prestige when every part fits");
        check(game.state["parts_prestige"].asInt() == 0 && game.state["equipped_parts"]["head"].isNull() &&
                  game.state["equipped_parts"]["binding"].isNull() &&
                  game.state["equipped_parts"]["rod"].isNull() &&
                  total(game.state["inventory_slots"], "pick_part") == 3,
              "lowered prestige returns all equipment with metadata");
        Value removeEnchant;
        removeEnchant["enchant"] = "fortune";
        removeEnchant["enchant_level"] = 0;
        check(game.customizeTestPick(removeEnchant).ok && !game.state["enchants"].isMember("fortune"),
              "zero custom enchant removes it");
        game.state["devmode"] = false;
        before = game.state;
        check(!game.giveTestItem(customCrystal).ok && !game.customizeTestPick(pickFields).ok &&
                  game.state == before,
              "direct customization APIs enforce devmode at mutation boundary");
        std::cout << "items: stacking, transfers, filter isolation, metadata, crafting, pets, multipliers, "
                     "rename, shop, autominer, command guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "items: " << error.what() << '\n';
        return 1;
    }
}
