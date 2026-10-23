#include "prison/world.hpp"
#include "ui_internal.hpp"
#include <cctype>

namespace prison {
void UI::Impl::setupDevFields() {
    if (!devChoices.isObject()) {
        devChoices["id"] = "crystal";
        devChoices["boost_type"] = "sell";
        devChoices["slot"] = "head";
        devChoices["rarity"] = "common";
        devChoices["pet_type"] = "Rabbit";
        devChoices["enchant"] = "efficiency";
        devChoices["size"] = "small";
        devChoices["pick_enchant"] = "efficiency";
        devChoices["stress"] = "mine";
        for (const auto* key : {"count", "tier", "level", "amount"})
            inputs[std::string("dev_") + key] = "1";
        inputs["dev_percent"] = "5";
        inputs["dev_duration"] = "3600";
    }
    refreshDevPick();
    inputs["dev_mine_prestige"] = std::to_string(
        game.state
            .get("dev_mine_prestige", Json(::Json::Int64(std::min<std::int64_t>(
                                          MineSizePrestigeCap, game.state["total_prestige"].asInt64()))))
            .asInt());
}

void UI::Impl::refreshDevPick() {
    inputs["dev_pick_name"] = game.state.get("pick_custom_name", "Prison Tool").asString();
    inputs["dev_pick_level"] = std::to_string(game.state.get("pick_level", 1).asInt());
    inputs["dev_pick_prestige"] = std::to_string(game.state.get("parts_prestige", 0).asInt() + 1);
    std::string enchant = devChoices.get("pick_enchant", "efficiency").asString();
    const int cap = game.data.enchants["defs"][enchant].get("max", 0).asInt();
    inputs["dev_pick_enchant_level"] = std::to_string(cap >= 0 ? cap : 100000);
}

std::string UI::Impl::devCaption(const std::string& value) const {
    if (game.data.enchants["defs"].isMember(value))
        return game.data.enchants["defs"][value].get("label", value).asString();
    if (value == "exp")
        return "EXP";
    std::string text = value;
    bool first = true;
    for (char& c : text) {
        if (c == '_') {
            c = ' ';
            first = true;
        } else if (first) {
            c = char(std::toupper(static_cast<unsigned char>(c)));
            first = false;
        }
    }
    return text;
}

void UI::Impl::devChoice(Rect box, const std::string& caption, const std::string& key,
                         std::vector<std::string> choices) {
    label(box.x, box.y, caption, 13);
    std::string value = devChoices.get(key, choices.empty() ? "" : choices.front()).asString();
    auto change = [this, key, choices, value](int direction) {
        if (choices.empty())
            return;
        auto found = std::find(choices.begin(), choices.end(), value);
        int index = found == choices.end() ? 0 : int(found - choices.begin());
        devChoices[key] = choices[(index + direction + int(choices.size())) % choices.size()];
        if (key == "pick_enchant")
            refreshDevPick();
        field.clear();
        SDL_StopTextInput();
    };
    button({box.x, box.y + 19, 30, 31}, "<", [change] { change(-1); });
    button({box.x + 35, box.y + 19, box.w - 70, 31}, devCaption(value), [change] { change(1); });
    button({box.x + box.w - 30, box.y + 19, 30, 31}, ">", [change] { change(1); });
}

void UI::Impl::devInput(Rect box, const std::string& caption, const std::string& key) {
    label(box.x, box.y, caption, 13);
    input({box.x, box.y + 19, box.w, 31}, "dev_" + key);
}

void UI::Impl::devToggle(Rect box, const std::string& caption, bool* value) {
    label(box.x, box.y, caption, 13);
    button({box.x, box.y + 19, box.w, 31}, *value ? "On" : "Off", [value] { *value = !*value; });
}

Json UI::Impl::devItemRequest() const {
    Json request;
    std::string id = devChoices.get("id", "crystal").asString();
    request["id"] = id;
    auto inputValue = [this](const char* key, const char* fallback) {
        auto entry = inputs.find(std::string("dev_") + key);
        return entry == inputs.end() ? std::string(fallback) : entry->second;
    };
    request["count"] = inputValue("count", "1");
    if (id == "crystal" || id == "crystal_dust" || id == "pick_part" || id == "minebomb")
        request["tier"] = inputValue("tier", "1");
    if (id == "crystal" || id == "pick_part")
        request["identified"] = devIdentified;
    if (((id == "crystal" || id == "pick_part") && devIdentified) || id == "pet" || id == "multiplier" ||
        id == "robot")
        request["boost_type"] = devChoices["boost_type"];
    if (((id == "crystal" || id == "pick_part") && devIdentified) || id == "pet")
        if (!devRoll)
            request["percent"] = inputValue("percent", "5");
    if (id == "pick_part")
        request["slot"] = devChoices["slot"];
    if (id == "pet" || id == "pet_egg")
        request["rarity"] = devChoices["rarity"];
    if (id == "pet") {
        request["pet_type"] = devChoices["pet_type"];
        request["level"] = inputValue("level", "1");
        request["mutation"] = devChoices.get("mutation", "none");
    }
    if (id == "robot")
        request["level"] = inputValue("level", "1");
    if (id == "enchant_book")
        request["enchant"] = devChoices["enchant"];
    if (id == "enchant_book" || id == "liquid_xp" || id == "multiplier")
        request["amount"] = inputValue("amount", "1");
    if (id == "multiplier" || id == "automine_time")
        request["duration"] = inputValue("duration", "3600");
    if (id == "pouch")
        request["size"] = devChoices["size"];
    return request;
}

double UI::Impl::devNumber(const Json& request, const char* key, double fallback, double maximum) const {
    try {
        std::string value = request.get(key, fallback).asString();
        std::size_t end = 0;
        double parsed = std::stod(value, &end);
        if (end == value.size() && std::isfinite(parsed))
            return std::clamp(parsed, 0.0, maximum);
    } catch (const std::exception&) {
    }
    return fallback;
}

void UI::Impl::devItemPreview(Rect box, const Json& request) {
    Json preview = request;
    std::string id = request["id"].asString();
    preview["count"] = int(devNumber(request, "count", 1, 99));
    preview["tier"] = int(devNumber(request, "tier", 1, 1000));
    preview["level"] = int(devNumber(request, "level", 1, id == "robot" ? 4000 : 75));
    if (id == "robot") {
        preview["boost_type"] = request.get("boost_type", "sell") == "sell"  ? "money"
                                : request.get("boost_type", "sell") == "exp" ? "xp"
                                                                             : "token";
        preview["fuel"] = 0;
    }
    preview["amount"] = devNumber(request, "amount", 1, 1e12);
    preview["duration"] = devNumber(request, "duration", 3600, 31536000);
    preview["boost"] = devNumber(request, "percent", 0, 1e6) / 100;
    preview["base_boost_pct"] = devNumber(request, "percent", 0, 1e6);
    preview["tier_id"] =
        request.get("rarity", "common") == "random" ? "common" : request.get("rarity", "common");
    preview["enchant_key"] = request.get("enchant", "efficiency");
    if (id == "pet") {
        preview["mood"] = "Joyful";
        preview["pet_color"] = "#ffffff";
    }
    if (id == "pouch")
        preview["tier"] = request.get("size", "small");
    draw.item({box.x, box.y, 56, 56}, preview, game.state.get("parts_prestige", 0).asInt(), glint);
    std::string caption = game.itemName(preview);
    if (request.get("rarity", "") == "random")
        caption = id == "pet" ? "Random Pet" : "Random Egg";
    if (request.get("rarity", "") == "random")
        draw.text(box.x + 72, box.y + 4, caption, itemColor(preview), 16);
    else
        itemName(box.x + 72, box.y + 4, preview, 16);
    std::string detail;
    if (request.isMember("identified") && !devIdentified)
        detail = "Identify after giving";
    else if ((id == "crystal" || id == "pick_part" || id == "pet") && devRoll)
        detail = "Boost rolls when given";
    else if (request.isMember("percent"))
        detail = "+" + request["percent"].asString() + "% " +
                 devCaption(request.get("boost_type", "sell").asString());
    else if (id == "enchant_book")
        detail = "Levels per book: " + request["amount"].asString();
    else if (id == "liquid_xp")
        detail = "EXP per item: " + request["amount"].asString();
    else if (request.isMember("duration"))
        detail = "Duration: " + request["duration"].asString() + " seconds";
    else if (id == "pouch")
        detail =
            std::to_string(
                game.data.systems["pouch_slots"].get(request.get("size", "small").asString(), 18).asInt()) +
            " slots";
    label(box.x + 72, box.y + 31, detail, 13);
}

void UI::Impl::devItems(Rect box) {
    float x = box.x + 24, y = box.y + 107, width = box.w - 48;
    const std::vector<std::string> ids = {"crystal",       "crystal_dust", "pick_part",    "pet_egg",
                                          "pet",           "pet_food",     "enchant_book", "liquid_xp",
                                          "automine_time", "multiplier",   "minebomb",     "pouch",
                                          "rename_token",  "robot",        "robot_fuel"};
    devChoice({x, y, width, 50}, "Item", "id", ids);
    Json request = devItemRequest();
    devItemPreview({x, y + 65, width, 60}, request);
    std::string id = request["id"].asString();
    int row = 0;
    float half = (width - 16) / 2;
    auto next = [&] {
        int index = row++;
        return Rect{x + (index % 2) * (half + 16), y + 138 + (index / 2) * 57, half, 50};
    };
    devInput(next(), "Count (1–99)", "count");
    if (id == "crystal" || id == "crystal_dust" || id == "pick_part" || id == "minebomb")
        devInput(next(), id == "minebomb" ? "Tier (1–20)" : "Tier (1–1000)", "tier");
    if (id == "crystal" || id == "pick_part")
        devToggle(next(), "Identified", &devIdentified);
    if (id == "pick_part")
        devChoice(next(), "Part", "slot", {"head", "binding", "rod"});
    if (id == "pet" || id == "pet_egg") {
        std::vector<std::string> rarities = {"random"};
        for (const auto& tier : game.data.systems["pets"]["tiers"])
            if (id == "pet" || tier.get("hatchable", true).asBool())
                rarities.push_back(tier["id"].asString());
        devChoice(next(), "Rarity", "rarity", rarities);
    }
    if (id == "pet") {
        std::vector<std::string> pets;
        for (const auto& kind : game.data.systems["pets"]["types"])
            pets.push_back(kind.asString());
        devChoice(next(), "Pet", "pet_type", pets);
        devInput(next(), "Pet Level (1–75)", "level");
        std::vector<std::string> mutations{"none"};
        for (const auto& trait : game.data.systems["mutations"]["traits"])
            mutations.push_back(trait["id"].asString());
        devChoice(next(), "Mutation", "mutation", mutations);
    }
    if (((id == "crystal" || id == "pick_part") && devIdentified) || id == "pet" || id == "multiplier" ||
        id == "robot")
        devChoice(next(), "Boost", "boost_type", {"sell", "token", "exp"});
    if (((id == "crystal" || id == "pick_part") && devIdentified) || id == "pet") {
        devToggle(next(), "Roll Boost", &devRoll);
        if (!devRoll)
            devInput(next(), "Boost (%)", "percent");
    }
    if (id == "robot")
        devInput(next(), "Robot Level (1–4000)", "level");
    if (id == "enchant_book") {
        std::vector<std::string> enchants;
        for (const auto& entry : game.data.enchants["order"])
            if (game.enchantCurrency(entry.asString()) == "tokens")
                enchants.push_back(entry.asString());
        devChoice(next(), "Enchant", "enchant", enchants);
        devInput(next(), "Levels per Book (1–10000)", "amount");
    }
    if (id == "liquid_xp")
        devInput(next(), "EXP per Item (1–1T)", "amount");
    if (id == "multiplier")
        devInput(next(), "Multiplier (1–100)", "amount");
    if (id == "multiplier" || id == "automine_time")
        devInput(next(), "Duration (seconds)", "duration");
    if (id == "pouch")
        devChoice(next(), "Size", "size", {"small", "medium", "large"});
    button({x, box.y + box.h - 72, width, 36}, "Give Item",
           [this] { apply(game.giveTestItem(devItemRequest())); });
}

void UI::Impl::devPick(Rect box) {
    float x = box.x + 24, y = box.y + 111, width = box.w - 48, half = (width - 16) / 2;
    devInput({x, y, width, 50}, "Name", "pick_name");
    devInput({x, y + 59, half, 50}, "Pick Level (1–10000)", "pick_level");
    devInput({x + half + 16, y + 59, half, 50}, "Parts Prestige (P1–P1000)", "pick_prestige");
    button({x, y + 120, width, 34}, "Apply Pickaxe", [this] {
        Json request;
        request["name"] = inputs["dev_pick_name"];
        request["level"] = inputs["dev_pick_level"];
        request["prestige"] = inputs["dev_pick_prestige"];
        auto result = game.customizeTestPick(request);
        if (result.ok)
            refreshDevPick();
        apply(result);
    });
    std::vector<std::string> enchants;
    for (const auto& enchant : game.data.enchants["order"])
        enchants.push_back(enchant.asString());
    devChoice({x, y + 178, half, 50}, "Enchant", "pick_enchant", enchants);
    devInput({x + half + 16, y + 178, half, 50}, "Enchant Level (0 removes)", "pick_enchant_level");
    button({x, y + 238, width, 34}, "Set Enchant", [this] {
        Json request;
        request["enchant"] = devChoices["pick_enchant"];
        request["enchant_level"] = inputs["dev_pick_enchant_level"];
        apply(game.customizeTestPick(request));
    });
    paragraph(x, y + 290, width, "Lowering parts prestige returns higher-tier parts to inventory.", 13);
    float third = (width - 20) / 3;
    const char* labels[] = {"Max Pickaxe", "Item Kit", "Test Funds"};
    const char* commands[] = {"/test max", "/test kit", "/test funds"};
    for (int i = 0; i < 3; ++i) {
        std::string commandText = commands[i];
        button({x + i * (third + 10), box.y + box.h - 72, third, 36}, labels[i],
               [this, commandText] { command(commandText); });
    }
}

void UI::Impl::devProgression(Rect box) {
    float x = box.x + 24, y = box.y + 121, width = box.w - 48, half = (width - 16) / 2;
    label(x, y,
          "Rank " + game.state.get("rank_name", "A").asString() + " · Prestige P" +
              number(game.state.get("prestige_level", 0).asDouble()),
          18);
    label(x, y + 34, "Money: " + compact(game.state.get("money", 0).asDouble()), 15);
    button({x, y + 96, half, 38}, "Rank Up", [this] { action("progression", "rankup"); });
    button({x + half + 16, y + 96, half, 38}, "Rank Up Max", [this] { action("progression", "rankupmax"); });
    button({x, y + 146, half, 38}, "Prestige", [this] { action("progression", "prestige"); });
    button({x + half + 16, y + 146, half, 38}, "Reset Mine", [this] { command("/resetmine"); });
    const int rank = game.state.get("rank_index", 0).asInt();
    const auto quote = rank < 25 ? "Next rank: " + std::string(1, char('B' + rank)) + " · " +
                                       compact(game.rankCost(rank + 1)) + " money"
                                 : "Next prestige: " + compact(game.prestigeCost()) + " money";
    paragraph(x, y + 214, width, quote, 14);
    button({x, y + 284, width, 36}, "Test Funds", [this] { command("/test funds"); });
}

void UI::Impl::devMine(Rect box) {
    float x = box.x + 24, y = box.y + 120, width = box.w - 48, half = (width - 16) / 2;
    label(x, y,
          "Current mine: " + std::to_string(ctx.mineSize.x) + " × " + std::to_string(ctx.mineSize.z) +
              " wide · " + std::to_string(ctx.mineSize.y) + " deep",
          18);
    label(x, y + 34, "Total prestige: " + number(game.state["total_prestige"].asDouble()), 15);
    devInput({x, y + 82, width, 50}, "Mine size prestige (0–50)", "mine_prestige");
    const bool available = !game.inShaft() && !ctx.resetting;
    button(
        {x, y + 145, half, 36}, "Apply Size",
        [this] { command("/test mine " + inputs["dev_mine_prestige"]); }, available);
    button({x + half + 16, y + 145, half, 36}, "Max Mine", [this] { command("/test mine max"); }, available);
    button({x, y + 198, width, 36}, "Use My Prestige", [this] { command("/test mine normal"); }, available);
    paragraph(x, y + 257, width,
              "Refills the mine at the selected size. Rank and prestige stay unchanged. "
              "Maximum size: 100 × 100 wide, 49 mineable blocks deep.",
              14);
}

void UI::Impl::devStress(Rect box) {
    float x = box.x + 24, y = box.y + 115, width = box.w - 48, half = (width - 16) / 2;
    devChoice({x, y, width, 50}, "Test", "stress", {"mine", "resetmine", "rankup", "prestige"});
    const Json& job = game.state["_stress"];
    bool active = job.get("active", false).asBool();
    label(x, y + 86, active ? "Running: " + job.get("type", "mine").asString() : "Stopped", 18);
    label(x, y + 123, "Iterations: " + number(job.get("iterations", 0).asDouble()), 15);
    paragraph(x, y + 169, width,
              "Stress tests repeat the selected operation and change this world. Use a disposable "
              "Devmode world.",
              14);
    button(
        {x, y + 260, half, 38}, "Start",
        [this] { command("/test stress " + devChoices.get("stress", "mine").asString()); }, !active);
    button({x + half + 16, y + 260, half, 38}, "Stop", [this] { command("/test stress stop"); }, active);
}

void UI::Impl::devtools() {
    if (!game.state.get("devmode", false).asBool()) {
        setPage("help");
        return;
    }
    Rect box = panel("Devmode", 730, 590);
    float x = box.x + 24, width = box.w - 48, tabWidth = (width - 32) / 5;
    const char* labels[] = {"Items", "Pickaxe", "Progression", "Mine", "Stress"};
    const char* tabs[] = {"items", "pick", "progression", "mine", "stress"};
    for (int i = 0; i < 5; ++i) {
        std::string tab = tabs[i];
        Rect buttonBox{x + i * (tabWidth + 8), box.y + 58, tabWidth, 34};
        button(buttonBox, labels[i], [this, tab] {
            devTab = tab;
            field.clear();
            SDL_StopTextInput();
            if (devTab == "pick")
                refreshDevPick();
        });
        if (devTab == tab)
            draw.border(buttonBox, foreground, 2);
    }
    if (devTab == "pick")
        devPick(box);
    else if (devTab == "progression")
        devProgression(box);
    else if (devTab == "mine")
        devMine(box);
    else if (devTab == "stress")
        devStress(box);
    else
        devItems(box);
}

} // namespace prison
