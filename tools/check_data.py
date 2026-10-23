#!/usr/bin/env python3
"""check the shared gameplay tables before building a package."""

import json
import math
import re
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate json key: {key}")
        result[key] = value
    return result


def numeric(value, minimum=0):
    return type(value) in (int, float) and math.isfinite(value) and value >= minimum


def finite_number(value):
    result = float(value)
    require(math.isfinite(result), "non-finite json number")
    return result


def unique(rows, key, label):
    values = [row[key] for row in rows]
    require(all(isinstance(value, str) and value for value in values), f"{label}: empty id")
    require(len(values) == len(set(values)), f"{label}: duplicate id")
    return set(values)


def check(root):
    textures = root / "textures"
    tables = {}
    for name in ("balance", "blocks", "crystals", "enchants", "help", "mine", "ranks", "source_systems", "native_economy", "native_content"):
        path = root / "data" / (name + ".json")
        require(path.stat().st_size <= 4 * 1024 * 1024, f"{name}: file too large")
        tables[name] = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=object_pairs,
                                 parse_float=finite_number, parse_constant=finite_number)
        require(isinstance(tables[name], list if name == "ranks" else dict), f"{name}: invalid root")

    for name in ("source_systems", "native_economy", "native_content"):
        require(tables[name].get("data_revision") == 2, f"{name}: expected data_revision 2")

    ranks = tables["ranks"]
    require([row["name"] for row in ranks] == list("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), "ranks: expected A-Z")
    costs = [row["cost"] for row in ranks]
    require(all(numeric(cost) for cost in costs) and costs == sorted(costs), "ranks: invalid costs")
    balance = tables["balance"]
    require(balance["schema_version"] == 1, "balance: unsupported schema")
    require(numeric(balance["currency_cap"], 1), "balance: invalid currency cap")
    for key in ("token_ore_chance", "proc_reward_ratio", "mine_auto_reset_ratio"):
        require(numeric(balance[key]) and balance[key] <= 1, f"balance: invalid {key}")
    require(0 <= balance["token_drop_min"] <= balance["token_drop_max"], "balance: invalid token range")

    enchants = tables["enchants"]
    economy = tables["native_economy"]
    currencies = economy["currencies"]
    require(set(currencies) == {"tokens", "rubies", "sapphires"}, "economy: unsupported wallets")
    for wallet, row in currencies.items():
        require(isinstance(row["label"], str) and row["label"] and
                isinstance(row["category"], str) and row["category"], f"{wallet}: missing labels")
        require(re.fullmatch(r"#[0-9a-fA-F]{6}", row["color"]), f"{wallet}: invalid color")
    require(set(economy["order"]) == {"ruby_greed", "sapphire_greed", "fuel_finder", "drill", "jackpot"} | set(economy["endgame"]),
            "economy: unknown new enchant")
    for key, definition in economy["defs"].items():
        require(key in enchants["defs"] or key in economy["order"], f"{key}: unknown override")
        require(definition["currency"] in currencies and definition["cost_curve"] == "linear",
                f"{key}: invalid currency/curve")
        enchants["defs"].setdefault(key, {}).update(definition)
    enchants["order"].extend(economy["order"])
    enchants["effects"].update(economy["effects"])
    require(enchants["defs"]["ruby_greed"]["currency"] == "tokens" and
            enchants["defs"]["sapphire_greed"]["currency"] == "rubies", "economy: currency unlock cycle")
    for key in ("ruby_greed", "sapphire_greed", "fuel_finder", "jackpot"):
        effect = economy["effects"][key]
        require(numeric(effect["chance_per_level"], 0) and 0 < effect["chance_cap"] <= 1,
                f"{key}: invalid drop chance")
    for key, ceiling in (("ruby_greed", .025), ("sapphire_greed", .005), ("fuel_finder", .002)):
        effect = economy["effects"][key]
        require(effect["chance_cap"] <= ceiling and
                effect["chance_per_level"] * economy["defs"][key]["max"] <= ceiling,
                f"{key}: premium drops exceed the intended rarity")
    for key in ("ruby_greed", "sapphire_greed"):
        require(type(economy["effects"][key]["amount"]) is int and
                1 <= economy["effects"][key]["amount"] <= 1e12, f"{key}: invalid gem reward")
    endgame = {"hyper_surge", "adrenaline", "jackpot", "rage", "credit_finder", "rewind", "energizer", "overdrive"}
    require(set(economy["endgame"]) == endgame and len(economy["endgame"]) == len(endgame),
            "endgame: unknown or duplicate enchant")
    for key in endgame:
        definition, effect = economy["defs"][key], economy["effects"][key]
        require(1000 <= definition["required_pickaxe_level"] <= 5000 and definition["max"] == 1000,
                f"{key}: invalid endgame unlock/cap")
        require(0 < effect["chance_cap"] <= 1/30000 and numeric(effect["chance_per_level"], 0) and
                300 <= effect["cooldown"] <= 900 and 0 <= effect["duration"] <= 30,
                f"{key}: invalid endgame probability/timers")
    require(economy["effects"]["rewind"]["income_seconds"] == 8, "rewind: bounded eight-second history required")
    require(set(economy["effects"]["energizer"]["activates"]) <=
            {"explosive", "jackhammer", "veinminer", "drill", "hyper_surge", "adrenaline", "rage", "overdrive"},
            "energizer: recursive or unsupported effect")
    drill = economy["effects"]["drill"]
    require(numeric(drill["drill_proc_base"]) and numeric(drill["drill_proc_per_level"]) and
            0 < drill["drill_proc_cap"] <= 1, "drill: invalid proc chance")
    robots = economy["robots"]
    require(robots["max_level"] == 4000 and robots["boost_per_level"] == .001 and
            robots["multiplier_cap"] == 5, "robots: expected 0.1% levels capped at 5x")
    require(robots["fuel_capacity"] == 1000000000 and robots["fuel_per_item"] == 250,
            "robots: invalid fuel storage")
    require(type(robots["upgrade_cost_multiplier"]) is int and
            1 <= robots["upgrade_cost_multiplier"] <= 1e12, "robots: invalid upgrade price factor")
    for key in ("fuel_level_step", "drop_pick_level", "upgrade_ruby_base", "upgrade_ruby_step",
                "upgrade_sapphire_start", "upgrade_sapphire_step"):
        require(type(robots[key]) is int and robots[key] > 0, f"robots: invalid {key}")
    require(numeric(robots["drop_chance"]) and robots["drop_chance"] <= 1, "robots: invalid drop chance")
    require(unique(robots["jackpot_loot"], "id", "jackpot") ==
            {"robot_fuel", "crystal", "pick_part", "pet_egg", "automine_time", "robot"},
            "jackpot: unsupported item")
    for entry in robots["jackpot_loot"]:
        require(numeric(entry["weight"], 1) and type(entry["count"]) is int and 1 <= entry["count"] <= 99,
                "jackpot: invalid loot weight/count")
    for texture in ("prisontest_robot.png", "prisontest_robot_fuel.png"):
        require((textures / texture).is_file(), f"robots: missing texture {texture}")
    order = enchants["order"]
    require(len(order) == len(set(order)) and set(order) == set(enchants["defs"]), "enchants: order/defs mismatch")
    for key, definition in enchants["defs"].items():
        require(isinstance(definition["label"], str) and definition["label"], f"{key}: missing label")
        require(re.fullmatch(r"#[0-9a-fA-F]{6}", definition["color"]), f"{key}: invalid color")
        require(type(definition["max"]) is int and definition["max"] >= -1, f"{key}: invalid maximum")
        require(numeric(definition["base_cost"]) and numeric(definition["step"]), f"{key}: invalid price")
        if "late_cost_scaling" in definition:
            late = definition["late_cost_scaling"]
            require(type(late["start"]) is int and late["start"] > 0 and
                    numeric(late["multiplier"], 1) and numeric(late["power"]),
                    f"{key}: invalid late price scaling")

    levels = tables["crystals"]["slot_levels"]
    require(len(levels) == 14 and all(type(level) is int and level > 0 for level in levels)
            and levels == sorted(set(levels)), "crystals: expected fourteen increasing unlock levels")
    content = tables["native_content"]
    resonance = content["resonance"]
    require(type(resonance["minimum_tier"]) is int and 5 <= resonance["minimum_tier"] <= 10,
            "resonance: invalid tier gate")
    require(unique(resonance["sets"], "id", "resonance") ==
            {"prosperity", "treasury", "insight", "harmony"}, "resonance: unsupported set")
    combinations = set()
    for row in resonance["sets"]:
        kinds = row["kinds"]
        require(len(kinds) == 3 and set(kinds) <= {"money", "token", "xp"},
                "resonance: three known crystal boosts required")
        combination = tuple(sorted(kinds))
        require(combination not in combinations, "resonance: duplicate combination")
        combinations.add(combination)
        require(all(numeric(row.get(kind, 0)) and row.get(kind, 0) <= .04
                    for kind in ("money", "token", "xp")), "resonance: bonus exceeds cap")
        require(row["label"] and row["description"], "resonance: missing text")
    mutations = content["mutations"]
    require(0 < mutations["chance"] <= .001, "mutations: invalid chance")
    require(unique(mutations["traits"], "id", "mutations") == {"efficient", "brood", "studious"},
            "mutations: unknown trait")
    for row in mutations["traits"]:
        require(re.fullmatch(r"#[0-9a-fA-F]{6}", row["color"]) and row["label"] and row["description"],
                "mutations: invalid color/text")
        bonuses = [row.get(kind, 0) for kind in ("fuel_saving", "eggs", "xp")]
        require(all(numeric(value) and value <= .05 for value in bonuses) and
                sum(value > 0 for value in bonuses) == 1, "mutations: one bounded bonus required")
    guardians = content["guardians"]
    require(type(guardians["required_pickaxe_level"]) is int and
            1000 <= guardians["required_pickaxe_level"] <= 5000 and
            0 < guardians["chance"] <= 1 / 50000, "guardians: invalid encounter gate/rate")
    require(type(guardians["health"]) is int and 3 <= guardians["health"] <= 3000 and
            guardians["health"] % 3 == 0 and .2 <= guardians["strike_seconds"] <= 2,
            "guardians: invalid health/strike speed")
    require(30 <= guardians["duration"] <= 600 and 600 <= guardians["cooldown"] <= 7200 and
            guardians["duration"] < guardians["cooldown"], "guardians: invalid timers")
    require(type(guardians["points"]) is int and 1 <= guardians["points"] <= 10,
            "guardians: invalid point reward")
    require(re.fullmatch(r"#[0-9a-fA-F]{6}", guardians["color"]) and guardians["label"],
            "guardians: invalid color/text")
    mine = tables["mine"]
    require(len(mine["blocks"]) == 52, "mine: expected smooth/ore pairs for A-Z")
    unique(mine["blocks"], "id", "mine")
    for index, block in enumerate(mine["blocks"]):
        rank = chr(ord("A") + index // 2)
        kind = "smooth" if index % 2 == 0 else "ore"
        require(block["unlock_rank"] == rank and block["id"] == f"tier_{rank.lower()}_{kind}",
                "mine: rank block pairs are out of order")
        require(numeric(block["value"], 1) and numeric(block["sell_value"]), "mine: invalid block value")
        for texture in re.findall(r"[a-zA-Z0-9_]+\.png", block["tile"]):
            require((textures / texture).is_file(), f"mine: missing texture {texture}")
    for axis, limit in (("x", 100), ("y", 50), ("z", 100)):
        low, high = mine["mine_min"][axis], mine["mine_max"][axis]
        require(type(low) is int and type(high) is int, f"mine: invalid {axis} bounds")
        span = high - low + (0 if axis == "y" else 1)
        require(2 <= span <= limit, f"mine: unsupported {axis} size")
        require(numeric(mine["spawn"][axis], -10000), f"mine: invalid spawn {axis}")

    unique(tables["blocks"]["blocks"], "id", "blocks")
    for block in [tables["blocks"]["default"], *tables["blocks"]["blocks"]]:
        require(numeric(block["money"]) and numeric(block["xp"]) and type(block["token_ore"]) is bool,
                "blocks: invalid reward")
    systems = tables["source_systems"]
    require(systems["pouch_slots"] == {"small": 18, "medium": 27, "large": 54}, "pouches: unsupported capacities")
    vault = systems["vault"]
    require(numeric(vault["page_base_cost"], 1) and numeric(vault["page_cost_multiplier"], 1),
            "vault: invalid page prices")
    require(set(systems["enchant_book_whitelist"]) <= set(order), "books: unknown enchant")
    pets = systems["pets"]
    unique(pets["tiers"], "id", "pets")
    require(pets["moods"] and pets["types"], "pets: missing moods or species")
    for tier in pets["tiers"]:
        require(numeric(tier["weight"]) and numeric(tier["hatch_blocks"], 1), "pets: invalid hatch values")
        require(numeric(tier["boost_min"]) and tier["boost_min"] <= tier["boost_max"], "pets: invalid boosts")
    require(1 <= systems["autominer"]["default_bps"] <= systems["autominer"]["max_bps"] <= 250,
            "autominer: invalid speed range")
    discoveries = systems["discoveries"]
    require(numeric(discoveries["pocket_chance"]) and discoveries["pocket_chance"] <= 1,
            "discoveries: invalid pocket_chance")
    require(type(discoveries["pocket_blocks"]) is int and 1 <= discoveries["pocket_blocks"] <= 7,
            "discoveries: invalid pocket size")
    require(re.fullmatch(r"#[0-9a-fA-F]{6}", discoveries["pocket_color"]),
            "discoveries: invalid pocket color")
    require(len(discoveries["themes"]) == 26 and
            all(isinstance(name, str) and name for name in discoveries["themes"]),
            "discoveries: expected one theme name per rank")

    regions = systems["regions"]
    require(unique(regions, "id", "regions") == {"quarry", "frost", "caverns", "ember"},
            "regions: unsupported mine ids")
    for region in regions:
        require(type(region["rebirth"]) is int and region["rebirth"] >= 0,
                "regions: invalid progression")
        require(region["layout"] in {"scattered", "seams", "bands"} and
                type(region["ore_offset"]) is int and 0 <= region["ore_offset"] <= 25 and
                numeric(region["ore_bonus"]) and region["ore_bonus"] <= .25,
                "regions: invalid terrain settings")
        require(re.fullmatch(r"#[0-9a-fA-F]{6}", region["color"]), "regions: invalid color")
    upgrades = systems["mine_upgrades"]
    require(unique(upgrades, "id", "mine upgrades") == {"ore_density", "pocket_size"},
            "upgrades: unknown upgrade")
    for upgrade in upgrades:
        cap = {"ore_density": 5, "pocket_size": 2}[upgrade["id"]]
        require(0 < len(upgrade["levels"]) <= cap, "upgrades: unsupported cap")
        previous_money, previous_points, previous_rebirth = 0, 0, 0
        for level in upgrade["levels"]:
            require(numeric(level["money"], 1) and level["money"] <= balance["currency_cap"] and
                    type(level["points"]) is int and 0 < level["points"] <= 9e15 and
                    type(level["rebirth"]) is int and level["rebirth"] >= 0, "upgrades: invalid price/gate")
            require(level["money"] > previous_money and level["points"] > previous_points and
                    level["rebirth"] >= previous_rebirth, "upgrades: prices/gates must increase")
            previous_money, previous_points, previous_rebirth = level["money"], level["points"], level["rebirth"]
    require(type(balance["crystal_drop_tier_cap"]) is int and 1 <= balance["crystal_drop_tier_cap"] <= 10,
            "crystals: unsupported tier cap")
    require(unique(systems["part_specializations"], "id", "specializations") ==
            {"plain", "brood", "prospector", "scholar"}, "parts: unsupported legacy specialization")
    for profile in systems["part_specializations"]:
        modifiers = profile["modifiers"]
        require(isinstance(modifiers, dict) and set(modifiers) <= {"money", "token", "xp", "eggs"},
                "parts: unsupported modifier")
        require(all(numeric(value, -.15) and value <= .20 for value in modifiers.values()),
                "parts: modifier outside supported range")
        require((not modifiers and profile["id"] == "plain") or
                (any(value > 0 for value in modifiers.values()) and any(value < 0 for value in modifiers.values())),
                "parts: specialization needs a benefit and a penalty")

    shaft = systems["shaft"]
    require(type(shaft["required_prestige"]) is int and shaft["required_prestige"] >= 1,
            "shaft: invalid unlock")
    require(type(shaft["base_blocks"]) is int and type(shaft["max_blocks"]) is int and
            1 <= shaft["base_blocks"] <= shaft["max_blocks"] <= 2000 and
            type(shaft["blocks_per_band"]) is int and 1 <= shaft["blocks_per_band"] <= 200,
            "shaft: invalid block target")
    require(type(shaft["clear_points"]) is int and 1 <= shaft["clear_points"] <= 5 and
            type(shaft["milestone_interval"]) is int and 2 <= shaft["milestone_interval"] <= 1000,
            "shaft: invalid clear rewards")
    require(unique(shaft["conditions"], "id", "shaft conditions") ==
            {"hardstone", "steady", "heavy", "veins"}, "shaft: unknown conditions")
    for condition in shaft["conditions"]:
        require(numeric(condition["dig_scale"], 1) and condition["dig_scale"] <= 2 and
                numeric(condition["speed_scale"], .5) and condition["speed_scale"] <= 1 and
                numeric(condition["ore_bonus"]) and condition["ore_bonus"] <= .15 and
                type(condition["procs"]) is bool and type(condition["bombs"]) is bool,
                "shaft: invalid condition effects")

    contracts = systems["contracts"]
    unique(contracts, "id", "contracts")
    require(len(contracts) >= 3, "contracts: need three choices")
    for contract in contracts:
        require(contract["kind"] in {"blocks", "ores", "hatches"},
                "contracts: unsupported goal")
        for key, maximum in (("goal", 9000000), ("points", 1900)):
            require(type(contract[key]) is int and 1 <= contract[key] <= maximum,
                    f"contracts: invalid {key}")

    expeditions = systems["expeditions"]
    require(type(expeditions["slots"]) is int and 1 <= expeditions["slots"] <= 3,
            "expeditions: unsupported slot count")
    require(unique(expeditions["routes"], "id", "expedition routes") == {row["id"] for row in regions},
            "expeditions: routes must match regions")
    assigned_species = set()
    tier_ids = {tier["id"] for tier in pets["tiers"]}
    for route in expeditions["routes"]:
        require(type(route["blocks"]) is int and 1 <= route["blocks"] <= 100000000 and
                type(route["points"]) is int and 1 <= route["points"] <= 980,
                "expeditions: invalid target/reward")
        species = set(route["species"])
        require(len(species) == len(route["species"]) and species <= set(pets["types"]) and
                not species.intersection(assigned_species), "expeditions: invalid/repeated species")
        assigned_species.update(species)
        loot = route["loot"]
        require(loot["id"] in {"pet_food", "pet_egg"} and type(loot["count"]) is int and
                1 <= loot["count"] <= 64, "expeditions: invalid loot")
        if loot["id"] == "pet_egg":
            require(loot["tier_id"] in tier_ids, "expeditions: unknown egg tier")
        else:
            require(numeric(loot["food_value"], 1), "expeditions: invalid food")

    help_data = tables["help"]
    require(help_data["version"] == 1, "help: unsupported version")
    pages = [page for section in help_data["sections"] for page in section["pages"]]
    unique(pages, "id", "help pages")
    unique(help_data["commands"], "command", "help commands")
    for page in pages:
        require(isinstance(page["title"], str) and page["title"], "help: missing title")
        require(isinstance(page["body"], list) and page["body"]
                and all(isinstance(line, str) and line for line in page["body"]), "help: invalid body")
    for command in help_data["commands"]:
        require(command.get("category") in {"Mining", "Ranks & Prestige", "Pickaxe", "Pets & Robots",
                                             "Items & Storage", "Settings & Chat", "Devmode"},
                "help: missing command category")
        require(isinstance(command["description"], str) and command["description"], "help: missing description")
        usage = command.get("usage", "/" + command["command"])
        require(isinstance(usage, str) and usage.split() and usage.split()[0] == "/" + command["command"],
                "help: usage does not match command")
        require(type(command.get("devmode", False)) is bool, "help: invalid developer visibility")
        aliases = command.get("aliases", [])
        require(isinstance(aliases, list) and all(isinstance(alias, str) and alias for alias in aliases),
                "help: invalid aliases")
    print(f"data: {len(tables)} json files, 26 ranks, 52 mine blocks, {len(order)} enchants and {len(pages)} help pages passed")


if __name__ == "__main__":
    try:
        check(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f"data check failed: {error}", file=sys.stderr)
        sys.exit(1)
