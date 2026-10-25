#include "prison/guardians.hpp"
#include "prison/world.hpp"
#include "ui_internal.hpp"
#include <array>
#include <cctype>
#include <iomanip>

namespace prison {
namespace {
bool validColor(const std::string& value) {
    if (value.size() != 7 || value[0] != '#')
        return false;
    return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) { return std::isxdigit(c); });
}
} // namespace

void UI::Impl::title() {
    draw.image({0, 0, draw.width(), draw.height()}, "ui/prisontest_menu_overlay.png");
    draw.rect({0, 0, draw.width(), draw.height()}, {0, 0, 0, 0.3f});
    const float logoWidth = std::min(500.f, draw.width() - 70);
    draw.image({(draw.width() - logoWidth) * 0.5f, std::max(35.f, draw.height() * 0.12f), logoWidth,
                logoWidth * 101 / 512},
               "ui/prisontest_header.png");
    const float x = (draw.width() - 300) * 0.5f;
    float y = std::max(210.f, draw.height() * 0.42f);
    pageButton({x, y, 300, 38}, "Singleplayer", "worlds");
    pageButton({x, y + 48, 300, 38}, "Settings", "settings");
    pageButton({x, y + 96, 300, 38}, "Credits", "credits");
    button({x, y + 144, 300, 38}, "Quit", [this] { action("quit"); });
    draw.text(12, draw.height() - 25, "prisontest " PRISON_VERSION, foreground, 13);
}

void UI::Impl::credits() {
    Rect box = panel("Credits", 620, 520);
    Rect list{box.x + 24, box.y + 62, box.w - 48, box.h - 86};
    beginScroll(list);
    float y = list.y - scroll;
    const std::pair<const char*, const char*> entries[] = {
        {"Pramsing", "Original concept."},
        {"void717doll", "Adaptation of the original concept."},
        {"Luanti", "Engine used by the original Prisontest game and a reference for this client."},
        {"Minetest Game", "Scenery and flower textures by its community artists. Full attribution is "
                          "included in ASSET_CREDITS.md and the texture licenses."},
        {"Minecraft and Roblox", "Their OP Prison game modes inspired the mining and progression."},
        {"Codex", "Help with coding and development."}};
    for (const auto& [name, contribution] : entries) {
        label(list.x, y, name, 17);
        y = paragraph(list.x, y + 25, list.w - 8, contribution, 14) + 18;
    }
    endScroll(y);
}

void UI::Impl::worlds() {
    Rect box = panel("Singleplayer", 610, 480);
    if (page == "create") {
        label(box.x + 28, box.y + 82, "World name");
        input({box.x + 28, box.y + 112, box.w - 56, 38}, "world_name");
        toggle({box.x + 28, box.y + 173, box.w - 56, 38}, "Devmode", "new_devmode");
        paragraph(box.x + 28, box.y + 230, box.w - 56,
                  "Devmode enables /give and /test in this world. Existing worlds keep their own setting.");
        button({box.x + 28, box.y + box.h - 67, box.w - 56, 38}, "Create World",
               [this] { action("new", inputs["world_name"].empty() ? "Your Mine" : inputs["world_name"]); });
        return;
    }
    Rect list{box.x + 22, box.y + 62, box.w - 44, box.h - 142};
    beginScroll(list);
    float y = list.y - scroll;
    for (const auto& world : ctx.worlds) {
        const std::string id = world.get("id", "").asString();
        button({list.x, y, list.w - 12, 58},
               world.get("name", "Your Mine").asString() + "   " +
                   clockText(world.get("playtime_seconds", 0).asDouble()),
               [this, id] { action("load", id); });
        y += 68;
    }
    if (ctx.worlds.empty())
        label(list.x + 12, y + 20, "No worlds yet.");
    endScroll(y);
    const float half = (box.w - 54) / 2;
    pageButton({box.x + 22, box.y + box.h - 63, half, 38}, "Create World", "create");
    pageButton({box.x + 32 + half, box.y + box.h - 63, half, 38}, "Import World", "import");
}

void UI::Impl::importWorld() {
    Rect box = panel("Import World", 610, 320);
    label(box.x + 24, box.y + 65, "Save file (.json)");
    input({box.x + 24, box.y + 93, box.w - 48, 36}, "import_path", "Paste a file path, or drop a save here");
    paragraph(box.x + 24, box.y + 148, box.w - 48,
              "Import a Godot or native world as a separate copy. The original file stays unchanged.", 15);
    button(
        {box.x + 24, box.y + box.h - 72, box.w - 48, 38}, "Import",
        [this] { action("import", inputs["import_path"]); }, !inputs["import_path"].empty());
}

void UI::Impl::pause() {
    Rect box = panel("Game Paused", 370, 400);
    float x = box.x + 28, y = box.y + 67, width = box.w - 56;
    pageButton({x, y, width, 38}, "Back to Game", "");
    pageButton({x, y + 48, width, 38}, "Settings", "settings");
    pageButton({x, y + 96, width, 38}, "Help", "help");
    pageButton({x, y + 144, width, 38}, "Tutorial", "tutorial");
    pageButton({x, y + 192, width, 38}, "Statistics", "statistics");
    button({x, y + 240, width, 38}, "Save & Return to Title", [this] { action("title"); });
}

void UI::Impl::statistics() {
    Rect box = panel("Statistics", 730, 570);
    const float x = box.x + 22, width = box.w - 44;
    label(x, box.y + 60, "Totals for this world", 14);
    std::vector<std::string> categories;
    for (const auto& entry : statisticDefinitions())
        if (std::find(categories.begin(), categories.end(), entry.category) == categories.end())
            categories.push_back(entry.category);
    if (categories.empty())
        return;
    if (std::find(categories.begin(), categories.end(), statisticsCategory) == categories.end())
        statisticsCategory = categories.front();
    const float categoryWidth = std::min(152.f, width * .25f);
    for (unsigned i = 0; i < categories.size(); ++i) {
        const auto category = categories[i];
        Rect tab{x, box.y + 94 + i * 34, categoryWidth, 28};
        button(tab, category, [this, category] {
            statisticsCategory = category;
            scroll = 0;
        });
        if (category == statisticsCategory)
            draw.border(tab, foreground, 2);
    }
    const float listX = x + categoryWidth + 18;
    Rect list{listX, box.y + 127, x + width - listX, box.h - 187};
    const bool enchants = statisticsCategory == "Enchants";
    label(list.x, box.y + 95, "Statistic", 14);
    label(list.x + list.w - draw.textWidth("Total", 14) - 10, box.y + 95, "Total", 14);
    draw.rect({list.x, box.y + 118, list.w, 1}, outline);
    beginScroll(list);
    float y = list.y - scroll;
    auto row = [&](const std::string& name, const std::string& value) {
        label(list.x, y + 5, name, 14);
        label(list.x + list.w - draw.textWidth(value, 14) - 10, y + 5, value, 14);
        y += 32;
    };
    for (const auto& entry : statisticDefinitions()) {
        if (entry.category != statisticsCategory)
            continue;
        const double total = game.statistic(entry.id);
        std::string value = entry.format == StatisticFormat::Time ? clockText(total)
                            : entry.format == StatisticFormat::Currency ? compact(total)
                                                                       : blockCount(total);
        if (entry.format == StatisticFormat::Currency && draw.textWidth(value, 14) > list.w * .45f) {
            std::ostringstream text;
            text << std::scientific << std::setprecision(2) << total;
            value = text.str();
        }
        row(entry.label, value);
    }
    if (enchants) {
        y += 8;
        label(list.x, y + 5, "Enchantment activations", 14);
        y += 32;
        for (const auto& id : procEnchantIds()) {
            const auto& definition = game.data.enchants["defs"][id];
            if (definition.isObject())
                row(definition.get("label", id).asString(), blockCount(game.enchantProcCount(id)));
        }
    }
    endScroll(y);
    paragraph(x, box.y + box.h - 43, width,
              "Older worlds only include activity that was recorded.", 12);
}

void UI::Impl::settings() {
    Rect box = panel(page == "itemviewer" ? "Item Viewer" : page == "theme" ? "Theme" : "Settings", 690, 555);
    float x = box.x + 22, y = box.y + 62;
    float width = box.w - 44, half = (width - 12) * 0.5f;
    if (page == "itemviewer") {
        slider({x, y, width, 38}, "Hand View", "hand_fov", 35, 110);
        slider({x, y + 50, width, 38}, "Item Size", "held_item_scale", 0.5, 2, "%", 100);
        slider({x, y + 100, width, 38}, "Tool Angle", "tool_angle", 0, 180);
        toggle({x, y + 150, width, 38}, "Left Hand", "left_handed");
        if (ctx.playing)
            draw.item({box.x + (box.w - 100) * 0.5f, y + 230, 100, 100},
                      game.state["inventory_slots"][game.state.get("selected_hotbar_index", 0).asInt()],
                      game.state.get("parts_prestige", 0).asInt(), glint);
        return;
    }
    if (page == "theme") {
        const char* keys[] = {"background", "foreground", "border", "glint"};
        const char* names[] = {"Background", "Foreground", "Border", "Enchant Glint"};
        for (int i = 0; i < 4; ++i) {
            std::string key = keys[i];
            label(x, y + 9, names[i]);
            input({x + 160, y, width - 220, 38}, key);
            draw.rect({x + width - 48, y + 2, 36, 34}, hexColor(inputs[key], foreground));
            y += 52;
        }
        button({x, y + 16, width, 38}, "Apply Colors", [this] {
            for (const auto* key : {"background", "foreground", "border", "glint"})
                if (!validColor(inputs[key])) {
                    apply({false, "Use a color such as #68686f.", ""});
                    return;
                }
            for (const auto* key : {"background", "foreground", "border", "glint"})
                ctx.settings[key] = inputs[key];
            settingsChanged();
        });
        return;
    }
    slider({x, y, half, 38}, "FOV", "fov", 50, 120);
    slider({x + half + 12, y, half, 38}, "GUI Scale", "gui_scale", 0.75, 1.5, "%", 100);
    slider({x, y + 50, half, 38}, "Sound", "master_volume", 0, 1, "%", 100);
    toggle({x + half + 12, y + 50, half, 38}, "VSync", "vsync");
    toggle({x, y + 100, half, 38}, "FPS", "show_fps");
    toggle({x + half + 12, y + 100, half, 38}, "Inventory Icon", "show_inventory_icon");
    toggle({x, y + 150, width, 38}, "MAX Label on Maxed Enchants", "show_max_labels");
    slider({x, y + 200, half, 38}, "Scoreboard", "scoreboard_opacity", 0, 1, "%", 100);
    slider({x + half + 12, y + 200, half, 38}, "Chat", "chat_opacity", 0, 1, "%", 100);
    pageButton({x, y + 250, half, 38}, "Item Viewer", "itemviewer");
    pageButton({x + half + 12, y + 250, half, 38}, "Theme", "theme");
    button(
        {x, y + 300, half, 38}, "Edit HUD",
        [this] {
            if (ctx.playing)
                setPage("hud");
        },
        ctx.playing);
    pageButton({x + half + 12, y + 300, half, 38}, "More Options", "bindings");
    slider({x, y + 350, half, 38}, "Sensitivity", "mouse_sensitivity", 0.1, 3.0, "%", 100);
    const bool visual = game.state["message_settings"].get("visual_enchants", false) == true;
    const Rect effects{x + half + 12, y + 350, half, 38};
    button(effects, std::string("Enchant Visuals: ") + (visual ? "On" : "Off"),
           [this, visual] { game.state["message_settings"]["visual_enchants"] = !visual; }, ctx.playing);
    hints.push_back({effects, "All mine enchants. On removes blocks; off keeps the rewards without changing terrain."});
}

void UI::Impl::bindings() {
    Rect box = panel("More Options", 550, 510);
    float x = box.x + 26, y = box.y + 65, width = box.w - 52;
    button({x, y, width, 36},
           std::string("Fullscreen: ") + (ctx.settings["fullscreen"].asBool() ? "On" : "Off"), [this] {
               ctx.settings["fullscreen"] = !ctx.settings["fullscreen"].asBool();
               action("fullscreen", ctx.settings["fullscreen"].asBool() ? "on" : "off");
               settingsChanged();
           });
    const char* keys[] = {"inventory_key", "pickaxe_key"};
    const char* names[] = {"Inventory", "Pickaxe"};
    for (int i = 0; i < 2; ++i) {
        std::string key = keys[i];
        std::string caption = std::string(names[i]) + ": " +
                              (listening == key ? "Press a key…" : SDL_GetKeyName(ctx.settings[key].asInt()));
        button({x, y + 104 + i * 46, width, 36}, caption, [this, key] { listening = key; });
    }
    const char* rows[] = {"W A S D     Move   /   Space     Jump",
                          "Left Ctrl     Sprint   /   T or /     Chat", "1–9 / Mouse Wheel     Hotbar",
                          "Right Shift     Edit HUD", "Esc     Close Menu / Pause"};
    for (int i = 0; i < 5; ++i)
        label(x, y + 215 + i * 30, rows[i], 14);
}

void UI::Impl::tool() {
    Rect box = panel("Pickaxe", 690, 550);
    draw.item({box.x + 30, box.y + 63, 72, 72}, game.state["inventory_slots"][0],
              game.state.get("parts_prestige", 0).asInt(), glint);
    itemName(box.x + 122, box.y + 70, game.state["inventory_slots"][0], 18);
    label(box.x + 122, box.y + 102,
          "Level " + std::to_string(game.state["pick_level"].asInt()) + "   EXP " +
              compact(game.state["pick_xp"].asDouble()) + "/" +
              compact(game.pickXpNeeded(game.state["pick_level"].asInt())));
    const float width = (box.w - 52) / 2;
    const char* names[] = {"Enchants", "Crystals", "Parts", "More"};
    const char* pages[] = {"enchants", "crystals", "parts", "more"};
    for (int i = 0; i < 4; ++i)
        pageButton({box.x + 20 + (i % 2) * (width + 12), box.y + 153 + (i / 2) * 46, width, 36}, names[i],
                   pages[i]);
    inventory(box);
}

void UI::Impl::enchants() {
    Rect box = panel("Enchant Pickaxe", 690, 540);
    const char* categories[] = {"tokens", "sapphires", "rubies"};
    const float tabWidth = (box.w - 56) / 3;
    for (int i = 0; i < 3; ++i) {
        const std::string key = categories[i];
        const auto& currency = game.data.systems["currencies"][key];
        Rect tab{box.x + 20 + i * (tabWidth + 8), box.y + 58, tabWidth, 33};
        button(tab, currency["category"].asString(), [this, key] {
            enchantCategory = key;
            scroll = 0;
        });
        if (key == enchantCategory)
            draw.border(tab, currencyColor(game.data, key), 2);
    }
    currencyLabel(box.x + 22, box.y + 106,
                  game.currencyName(enchantCategory) + ": " +
                      compact(game.state.get(enchantCategory, 0).asDouble()));
    Rect list{box.x + 20, box.y + 141, box.w - 40, box.h - 174};
    beginScroll(list);
    int columns = std::clamp(int((list.w + 8) / 196), 1, 3);
    float cardWidth = (list.w - (columns - 1) * 8 - 8) / columns;
    int index = 0;
    for (const auto& entry : game.data.enchants["order"]) {
        std::string key = entry.asString();
        if (game.enchantCurrency(key) != enchantCategory)
            continue;
        const Json& def = game.data.enchants["defs"][key];
        int level = game.state["enchants"].get(key, 0).asInt();
        int maximum = def.get("max", -1).asInt();
        bool maxed = maximum >= 0 && level >= maximum;
        int required = def.get("required_pickaxe_level", 1).asInt();
        double cost = game.enchantCost(key, 1);
        bool locked = required > game.state["pick_level"].asInt();
        bool affordable = game.state.get(enchantCategory, 0).asDouble() >= cost;
        bool enabled = !locked && !maxed && affordable;
        Rect card{list.x + (index % columns) * (cardWidth + 8), list.y - scroll + (index / columns) * 64,
                  cardWidth, 56};
        button(card, "", [this, key] {
            selectedEnchant = key;
            buyAmount = 1;
            inputs["enchant_amount"] = "1";
            setPage("enchant_detail");
        });
        Color iconTint = maxed         ? hexColor("#f5d58b")
                         : locked      ? hexColor("#797982")
                         : !affordable ? hexColor("#cd8e8e")
                                       : Color{};
        draw.image({card.x + 8, card.y + 12, 32, 32}, "textures/prisontest_enchant_book.png", iconTint);
        draw.text(card.x + 47, card.y + 7, def.get("label", key).asString(),
                  enabled ? foreground : dim(foreground, .6f), 14);
        draw.text(card.x + 47, card.y + 30,
                  std::to_string(level) + " / " + (maximum < 0 ? "No cap" : std::to_string(maximum)),
                  enabled ? foreground : dim(foreground, .6f), 13);
        if (visible(card))
            hints.push_back({card, locked  ? "Requires level " + std::to_string(required)
                                   : maxed ? def.get("label", key).asString() + ": " + std::to_string(level) +
                                                 " / " + std::to_string(maximum)
                                           : (affordable ? "Next level: " : "Can't afford: ") +
                                                 compact(cost) + " " + game.currencyName(enchantCategory)});
        ++index;
    }
    endScroll(list.y - scroll + ((index + columns - 1) / columns) * 64);
}

int UI::Impl::remainingEnchant() const {
    const int maximum = game.data.enchants["defs"][selectedEnchant].get("max", -1).asInt();
    return maximum < 0
               ? 20000
               : std::clamp(maximum - game.state["enchants"].get(selectedEnchant, 0).asInt(), 0, 20000);
}

void UI::Impl::setAmount(int value) {
    buyAmount = std::clamp(value, 1, std::max(1, remainingEnchant()));
    inputs["enchant_amount"] = std::to_string(buyAmount);
}

void UI::Impl::maximumAmount() {
    int low = 0, high = remainingEnchant();
    double balance = game.state.get(game.enchantCurrency(selectedEnchant), 0).asDouble();
    while (low < high) {
        int mid = low + (high - low + 1) / 2;
        if (game.enchantCost(selectedEnchant, mid) <= balance)
            low = mid;
        else
            high = mid - 1;
    }
    setAmount(low);
}

void UI::Impl::enchantDetail() {
    Rect box = panel("Enchant Pickaxe", 565, 515);
    const Json& def = game.data.enchants["defs"][selectedEnchant];
    const std::string wallet = game.enchantCurrency(selectedEnchant);
    int level = game.state["enchants"].get(selectedEnchant, 0).asInt();
    int cap = def.get("max", -1).asInt();
    int required = def.get("required_pickaxe_level", 1).asInt();
    bool locked = game.state["pick_level"].asInt() < required;
    if (buyAmount > std::max(1, remainingEnchant()))
        setAmount(buyAmount);
    auto& price = prices[selectedEnchant];
    if (price.level != level || price.count != buyAmount)
        price = {level, buyAmount, game.enchantCost(selectedEnchant, buyAmount)};
    float x = box.x + 24, width = box.w - 48;
    label(x, box.y + 62,
          def.get("label", selectedEnchant).asString() + " · Level " + std::to_string(level) + " / " +
              (cap < 0 ? "No cap" : std::to_string(cap)),
          18);
    currencyLabel(x, box.y + 94,
                  game.currencyName(wallet) + ": " + compact(game.state.get(wallet, 0).asDouble()));
    if (locked)
        label(x, box.y + 125, "Requires pickaxe level " + std::to_string(required), 14);
    paragraph(x, box.y + 149, width, def.get("description", "").asString(), 14);
    const auto& effect = game.data.enchants["effects"][selectedEnchant];
    if (effect.isMember("cooldown")) {
        const double chance = std::min(effect["chance_cap"].asDouble(),
                                       std::max(1, level) * effect["chance_per_level"].asDouble());
        const std::string odds = (level == 0 ? "Level 1: 1 in " : "Proc: 1 in ") +
                                 blockCount(std::ceil(1 / std::max(chance, 1e-15)));
        label(x, box.y + 189, odds + " blocks · Cooldown " + clockText(effect["cooldown"].asDouble()), 13);
    }
    label(x, box.y + 228, "Levels to buy:", 16);
    input({x + 132, box.y + 217, width - 230, 35}, "enchant_amount");
    button({x + width - 85, box.y + 217, 85, 35}, "Set", [this] {
        const std::string& text = inputs["enchant_amount"];
        if (text.empty() || text.size() > 6 ||
            !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
            apply({false, "Enter a whole number of levels.", ""});
            return;
        }
        setAmount(std::stoi(text));
        field.clear();
        SDL_StopTextInput();
    });
    const int changes[] = {-100, -10, -1, 1, 10, 100};
    float buttonWidth = (width - 6 * 7) / 7;
    for (int i = 0; i < 6; ++i) {
        int amount = changes[i];
        button({x + i * (buttonWidth + 7), box.y + 275, buttonWidth, 36},
               (amount > 0 ? "+" : "") + std::to_string(amount),
               [this, amount] { setAmount(buyAmount + amount); });
    }
    button({x + 6 * (buttonWidth + 7), box.y + 275, buttonWidth, 36}, "MAX", [this] { maximumAmount(); });
    std::string cost = "Cost: " + compact(price.cost) + " " + game.currencyName(wallet);
    currencyLabel(box.x + (box.w - draw.textWidth(cost, 16)) / 2, box.y + 338, cost);
    bool affordable = game.state.get(wallet, 0).asDouble() >= price.cost;
    button(
        {x + width / 2 - 100, box.y + 378, 200, 40}, locked ? "Locked by pick level" : "Buy",
        [this] {
            apply(game.buyEnchant(selectedEnchant, buyAmount));
            setAmount(buyAmount);
        },
        !locked && remainingEnchant() > 0 && affordable);
    pageButton({x, box.y + box.h - 62, 100, 32}, "Back", "enchants");
}

void UI::Impl::equipment() {
    Rect box = panel(page == "parts" ? "Pickaxe Parts" : "Crystals", 690, 590);
    float center = box.x + box.w * 0.5f;
    if (page == "parts") {
        const char* keys[] = {"head", "binding", "rod"};
        const char* labels[] = {"Head", "Binding", "Rod"};
        for (int i = 0; i < 3; ++i) {
            label(center - 110, box.y + 72 + i * 54, labels[i]);
            slot({center + 20, box.y + 60 + i * 54, 46, 46}, "parts", i,
                 game.state["equipped_parts"][keys[i]]);
        }
        label(box.x + 24, box.y + 226,
              "Parts Prestige: P" + std::to_string(game.state.get("parts_prestige", 0).asInt() + 1));
        button({box.x + box.w - 210, box.y + 214, 185, 34}, "Prestige Parts",
               [this] { apply(game.prestigeParts()); });
    } else {
        int count = game.crystalSlots();
        const int columns = 9;
        const float start = center - std::min(count, columns) * 48 * 0.5f;
        label(box.x + 24, box.y + 62, "Equipped (drag in/out)", 15);
        for (int i = 0; i < count; ++i) {
            float x = start + (i % columns) * 48, y = box.y + 96 + (i / columns) * 48;
            slot({x, y, 44, 44}, "crystals", i, game.state["equipped_crystals"][i]);
        }
        if (count < int(game.data.crystals["slot_levels"].size()))
            label(box.x + 24, box.y + 200,
                  "Slot " + std::to_string(count + 1) + " unlocks at Level " +
                      std::to_string(game.data.crystals["slot_levels"][count].asInt()),
                  15);
        label(box.x + 24, box.y + 225,
              "Resonance: " + game.crystalResonance().get("label", "None").asString(), 14);
        button({center - 90, box.y + 250, 180, 28}, "Unequip All", [this, count] {
            for (int i = count - 1; i >= 0; --i)
                if (game.state["equipped_crystals"][i].isObject()) {
                    auto result = game.transfer("crystals", i, "inventory");
                    if (!result.ok) {
                        apply(result);
                        break;
                    }
                }
        });
    }
    if (page == "crystals")
        pageButton({box.x + 24, box.y + 286, 160, 30}, "Resonance", "resonance");
    inventory(box);
}

void UI::Impl::resonance() {
    Rect box = panel("Crystal Resonance", 620, 420);
    Rect list{box.x + 24, box.y + 60, box.w - 48, box.h - 138};
    beginScroll(list);
    float y = paragraph(list.x, list.y - scroll, list.w,
                        "Use the first three crystal slots. All three must be identified, the same tier, "
                        "and T5 or higher.") +
              12;
    const auto& active = game.crystalResonance();
    label(list.x, y, "Active: " + active.get("label", "None").asString());
    y += 32;
    for (const auto& set : game.data.systems["resonance"]["sets"]) {
        label(list.x, y, set["label"].asString(), 16);
        y = paragraph(list.x, y + 22, list.w, set["description"].asString(), 14) + 16;
    }
    endScroll(y);
    pageButton({box.x + 24, box.y + box.h - 56, 160, 32}, "Crystals", "crystals");
}

void UI::Impl::guardian() {
    Rect box = panel("Mine Guardian", 620, 430);
    const auto& state = game.state["guardian"];
    const auto& cfg = game.data.systems["guardians"];
    const float x = box.x + 24, width = box.w - 48;
    Rect list{x, box.y + 62, width, box.h - 142};
    beginScroll(list);
    float y = paragraph(x, list.y - scroll, width,
                        "Stone Guardians can awaken at the mine entrance while manually mining at "
                        "pickaxe level 1,000 or higher.") +
              14;
    y = paragraph(x, y, width,
                  "Mine the bright arm, then the other arm, then the head. Only exposed weak points take "
                  "damage. Finish within three minutes.") +
        14;
    label(x, y, "Defeated: " + state["defeated"].asString(), 15);
    y += 32;
    y = paragraph(x, y, width,
                  "Each defeat gives " + cfg.get("points", 3).asString() +
                      " points for the Point Shop. Leaving or resetting ends the fight "
                      "without a reward.",
                  14) +
        14;
    if (state["active"].asBool()) {
        label(x, y, guardianWeakName(state) + " exposed · " + clockText(state["remaining"].asDouble()), 15);
    } else
        label(x, y, "Next encounter eligible in " + clockText(state["cooldown"].asDouble()), 14);
    endScroll(y + 28);
    if (state["active"].asBool()) {
        button(
            {x, box.y + box.h - 60, 158, 34}, "Go to Guardian",
            [this] {
                action("mine_entry");
                setPage("");
            },
            !game.state["autominer_active"].asBool());
        button({x + 170, box.y + box.h - 60, 150, 34}, "Leave Fight", [this] {
            apply(game.command("/guardian leave"));
            setPage("");
        });
    }
    if (game.state["devmode"].asBool() && !state["active"].asBool())
        button(
            {box.x + box.w - 205, box.y + box.h - 60, 180, 34}, "Summon (Devmode)",
            [this] { apply(game.command("/test guardian")); }, !state["active"].asBool());
}

void UI::Impl::robots() {
    Rect box = panel("Robots", 690, 610);
    currencyLabel(box.x + 22, box.y + 60,
                  "Rubys: " + compact(game.state["rubies"].asDouble()) +
                      "   Sapphires: " + compact(game.state["sapphires"].asDouble()),
                  14);
    Rect list{box.x + 22, box.y + 95, box.w - 44, box.h - 333};
    beginScroll(list);
    const char* names[] = {"SellBoost Robot", "EXPBoost Robot", "TokenBoost Robot"};
    for (int i = 0; i < 3; ++i) {
        const Json& robot = game.state["robot_slots"][i];
        const float y = list.y - scroll + i * 83;
        slot({list.x, y + 8, 46, 46}, "robots", i, robot);
        draw.text(list.x + 58, y, names[i], robot.isObject() ? itemColor(robot) : foreground, 15);
        if (!robot.isObject()) {
            label(list.x + 58, y + 25, "Drag a robot here to equip it.", 13);
            continue;
        }
        label(list.x + 58, y + 23,
              "Level " + std::to_string(robot["level"].asInt()) + "  +" +
                  number((game.robotMultiplier(robot) - 1) * 100, 1) + "%",
              13);
        label(list.x + 58, y + 44,
              "Fuel: " + blockCount(robot["fuel"].asDouble()) + "  (" +
                  std::to_string(game.robotFuelCost(robot)) + "/block)",
              13);
        const float right = list.x + list.w - 188;
        Json args;
        args["index"] = i;
        button({right, y + 3, 176, 29}, "Load Fuel",
               [this, args] { apply(game.itemAction("robot_refuel", args)); });
        for (int b = 0; b < 2; ++b) {
            const int levels = b == 0 ? 1 : 10;
            button(
                {right + b * 90, y + 38, 86, 29}, "+" + std::to_string(levels),
                [this, i, levels] {
                    const auto quote = game.robotUpgradeQuote(i, levels);
                    if (quote.isMember("error")) {
                        apply({false, quote["error"].asString()});
                        return;
                    }
                    confirmAction = "robot_upgrade";
                    confirmArgs = quote;
                    setPage("confirm");
                },
                robot["level"].asInt() < game.data.systems["robots"]["max_level"].asInt());
        }
    }
    endScroll(list.y - scroll + 3 * 83);
    inventory(box);
}

void UI::Impl::pets() {
    Rect box = panel("Pets & Hatchery", 690, 590);
    const float x = box.x + 22, y = box.y + 61;
    label(x, y, "Active Pet", 15);
    slot({x, y + 27, 52, 52}, "pet", 0, game.state["pet_data"]["active_pet"]);
    label(x + 82, y, "Food", 15);
    slot({x + 82, y + 27, 52, 52}, "pet_food", 0, Json());
    const Json& pet = game.state["pet_data"]["active_pet"];
    if (pet.isObject()) {
        draw.text(x + 150, y + 29, game.itemName(pet), itemColor(pet), 16);
        label(x + 150, y + 54,
              "Level " + std::to_string(pet.get("level", 1).asInt()) + "/75   EXP " +
                  std::to_string(pet.get("exp", 0).asInt()) + "/100",
              14);
        const auto& mutation = game.petMutation(pet);
        if (mutation.isObject())
            draw.text(x + 150, y + 76, mutation["label"].asString(), hexColor(mutation["color"].asString()),
                      13);
    } else
        label(x + 150, y + 41, "Drag a pet here to equip.", 14);
    button({box.x + box.w - 160, y + 25, 135, 36}, "Merge Pets", [this] {
        confirmAction = "merge_pets";
        confirmArgs = Json();
        setPage("confirm");
    });
    pageButton({box.x + box.w - 160, y + 70, 135, 29}, "Make Pet Food", "pet_food");
    label(x, y + 93, "Hatchery", 15);
    const Json& eggs = game.state["pet_data"]["hatchery"];
    const float start = box.x + (box.w - 6 * 70) * 0.5f;
    for (int i = 0; i < 6; ++i) {
        Json egg = eggs[i];
        if (egg.isObject())
            egg["id"] = egg.get("ready", false).asBool() ? "pet" : "pet_egg";
        slot({start + i * 70, y + 119, 50, 50}, "hatchery", i, egg);
        std::string caption = !egg.isObject() ? "Add egg"
                              : egg.get("ready", false).asBool()
                                  ? "Ready"
                                  : number(100 * egg.get("progress", 0).asDouble() /
                                           std::max(1.0, egg.get("required", 1).asDouble())) +
                                        "%";
        label(start + i * 70, y + 173, caption, 12);
    }
    pageButton({box.x + box.w - 180, box.y + 285, 154, 32}, "Expeditions", "expeditions");
    inventory(box);
}

void UI::Impl::storage() {
    std::string container = page;
    Json rows;
    std::string title = "Inventory";
    int count = 0;
    if (page == "vault") {
        title = "Player Vault";
        rows = game.vaultSlots(vaultIndex);
        container = "vault:" + std::to_string(vaultIndex);
        count = VaultPageSize;
    }
    if (page.rfind("pouch:", 0) == 0) {
        int index = -1;
        try {
            index = std::stoi(page.substr(6));
        } catch (const std::exception&) {
        }
        if (index <= 0 || index >= int(game.state["inventory_slots"].size()) ||
            game.state["inventory_slots"][index].get("id", "") != "pouch") {
            setPage("inventory");
            return;
        }
        const Json& pouch = game.state["inventory_slots"][index];
        title = game.itemName(pouch);
        rows = pouch["items"];
        count = std::clamp(pouch.get("slots", 18).asInt(), 0, 54);
    }
    const float size = 43;
    const float storageHeight = ((count + 8) / 9) * size;
    const float controls = page == "vault" ? 82 : 0;
    Rect box = panel(title, count ? 9 * size + 48 : 690, count ? storageHeight + 299 + controls : 400);
    if (page == "vault") {
        const float x = box.x + 24, y = box.y + 57, width = box.w - 48;
        input({x, y, width - 94, 32}, "vault_name");
        button({x + width - 86, y, 86, 32}, "Rename", [this] {
            Json args;
            args["page"] = vaultIndex;
            args["name"] = inputs["vault_name"];
            apply(game.itemAction("vault_rename", args));
        });
        button(
            {x, y + 40, 32, 32}, "<", [this] { setPage("vault:" + std::to_string(vaultIndex - 1)); },
            vaultIndex > 0);
        label(x + 40, y + 47, std::to_string(vaultIndex + 1) + " / " + std::to_string(game.vaultPages()), 15);
        button(
            {x + 109, y + 40, 32, 32}, ">", [this] { setPage("vault:" + std::to_string(vaultIndex + 1)); },
            vaultIndex + 1 < game.vaultPages());
        const bool canBuy = game.vaultPages() < MaxVaultPages;
        button(
            {x + 155, y + 40, width - 155, 32},
            canBuy ? "Buy page · " + compact(game.nextVaultCost()) : "All pages unlocked",
            [this] {
                confirmAction = "vault_buy";
                confirmArgs = Json();
                confirmArgs["page"] = game.vaultPages();
                confirmArgs["cost"] = game.nextVaultCost();
                setPage("confirm");
            },
            canBuy);
    }
    if (count) {
        const float start = box.x + (box.w - 9 * size) * 0.5f;
        Rect top{start, box.y + 57 + controls, 9 * size, box.h - 299 - controls};
        beginScroll(top);
        for (int i = 0; i < count; ++i)
            slot({start + (i % 9) * size, top.y - scroll + (i / 9) * size, size - 2, size - 2}, container, i,
                 rows[i]);
        endScroll(top.y - scroll + storageHeight);
    } else {
        pageButton({box.x + 24, box.y + 65, 140, 38}, "Pickaxe", "pick");
        pageButton({box.x + 174, box.y + 65, 140, 38}, "Pets", "pets");
        pageButton({box.x + 324, box.y + 65, 140, 38}, "Vault", "vault");
        pageButton({box.x + 474, box.y + 65, 140, 38}, "More", "more");
    }
    inventory(box);
}

void UI::Impl::more() {
    Rect box = panel("More", 600, 570);
    const std::pair<const char*, const char*> entries[] = {{"Pets", "pets"},
                                                           {"Player Vault", "vault"},
                                                           {"Point Shop", "pointshop"},
                                                           {"Autominer", "automine"},
                                                           {"Robots", "robots"},
                                                           {"Filter", "filter"},
                                                           {"Bin", "bin"},
                                                           {"Boosts", "boosts"},
                                                           {"Statistics", "statistics"},
                                                           {"Rewards", "rewards"},
                                                           {"Combine Multipliers", "combinemulti"},
                                                           {"Settings", "settings"},
                                                           {"Mines", "mines"},
                                                           {"Mine Upgrades", "mine_upgrades"},
                                                           {"Contracts", "contracts"},
                                                           {"Expeditions", "expeditions"},
                                                           {"Equipment Sets", "equipment_sets"},
                                                           {"Challenge Shaft", "shaft"},
                                                           {"Mine Guardian", "guardian"},
                                                           {"Crystal Resonance", "resonance"}};
    float width = (box.w - 60) * 0.5f;
    Rect list{box.x + 24, box.y + 66, box.w - 48, box.h - 210};
    beginScroll(list);
    const int count = int(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; ++i)
        pageButton({box.x + 24 + (i % 2) * (width + 12), list.y - scroll + (i / 2) * 46, width, 38},
                   entries[i].first, entries[i].second);
    endScroll(list.y - scroll + ((count + 1) / 2) * 46);
    button({box.x + 24, box.y + box.h - 124, width, 38}, "Rank Up",
           [this] { action("progression", "rankup"); });
    button({box.x + 36 + width, box.y + box.h - 124, width, 38}, "Rank Up Max",
           [this] { action("progression", "rankupmax"); });
    button({box.x + 24, box.y + box.h - 76, width, 38}, "Prestige",
           [this] { action("progression", "prestige"); });
    if (game.state.get("devmode", false).asBool())
        pageButton({box.x + 36 + width, box.y + box.h - 76, width, 38}, "Devmode", "test");
}

void UI::Impl::mines() {
    Rect box = panel("Mines", 690, 570);
    const float x = box.x + 24, width = box.w - 48;
    Rect list{x, box.y + 62, width, box.h - 142};
    beginScroll(list);
    float y = list.y - scroll;
    for (const auto& region : game.data.systems["regions"]) {
        const auto id = region["id"].asString();
        const bool unlocked = game.state["rebirth"].asDouble() >= region["rebirth"].asInt();
        const bool current = game.state["progression"]["mine"] == id;
        label(x, y, region["label"].asString() + " · Rebirth " + region["rebirth"].asString(), 17);
        paragraph(x, y + 26, width - 142, region["description"].asString(), 14);
        button(
            {x + width - 126, y + 22, 118, 34},
            current    ? "Current"
            : unlocked ? "Enter"
                       : "Locked",
            [this, id] {
                confirmAction = "mine_select";
                confirmArgs = Json();
                confirmArgs["id"] = id;
                setPage("confirm");
            },
            unlocked && !current);
        y += 99;
    }
    endScroll(y);
    pageButton({x, box.y + box.h - 60, 180, 34}, "Mine Upgrades", "mine_upgrades");
    pageButton({x + 190, box.y + box.h - 60, 180, 34}, "Challenge Shaft", "shaft");
}

void UI::Impl::mineUpgrades() {
    Rect box = panel("Mine Upgrades", 690, 510);
    const float x = box.x + 24, width = box.w - 48;
    label(x, box.y + 61, "Permanent upgrades. Changes apply on the next refill.", 14);
    Rect list{x, box.y + 94, width, box.h - 120};
    beginScroll(list);
    float y = list.y - scroll;
    for (const auto& upgrade : game.data.systems["mine_upgrades"]) {
        const auto quote = game.mineUpgradeQuote(upgrade["id"].asString());
        const bool maximum = quote["level"] == quote["max"];
        label(x, y,
              upgrade["label"].asString() + " · " + quote["level"].asString() + "/" + quote["max"].asString(),
              17);
        label(x, y + 26, upgrade["description"].asString(), 14);
        if (!maximum)
            label(x, y + 53,
                  compact(quote["money"].asDouble()) + " money · " + quote["points"].asString() +
                      " points · Rebirth " + quote["rebirth"].asString(),
                  13);
        button(
            {x + width - 120, y + 13, 112, 34}, maximum ? "Complete" : "Upgrade",
            [this, quote] {
                confirmAction = "mine_upgrade";
                confirmArgs = quote;
                setPage("confirm");
            },
            !maximum);
        y += 108;
    }
    endScroll(y);
}

void UI::Impl::equipmentSets() {
    Rect box = panel("Equipment Sets", 690, 440);
    paragraph(box.x + 24, box.y + 60, box.w - 48,
              "Save your parts, crystals and active pet. Saved items must be equipped or in your inventory.",
              14);
    for (int i = 0; i < 3; ++i) {
        const float y = box.y + 124 + i * 91;
        const auto key = "set_name_" + std::to_string(i);
        input({box.x + 24, y, 224, 34}, key);
        button({box.x + 262, y, 175, 34}, "Save Current", [this, i, key] {
            Json args;
            args["index"] = i;
            args["name"] = inputs[key];
            apply(game.itemAction("equipment_set_save", args));
        });
        button(
            {box.x + 451, y, 140, 34}, "Equip",
            [this, i] {
                Json args;
                args["index"] = i;
                apply(game.itemAction("equipment_set_equip", args));
            },
            game.state["equipment_sets"][i]["saved"].asBool());
        label(box.x + 24, y + 41,
              game.state["equipment_sets"][i]["saved"].asBool() ? "Equipment saved" : "Empty set", 13);
    }
}

void UI::Impl::shaft() {
    Rect box = panel("Challenge Shaft", 670, 445);
    const float x = box.x + 24, width = box.w - 48;
    const auto& saved = game.state["shaft"];
    const bool active = game.inShaft();
    const Json floor =
        active ? saved["floor"] : game.shaftFloor(std::min(1000000000, saved["best_depth"].asInt() + 1));
    Json condition;
    for (const auto& row : game.data.systems["shaft"]["conditions"])
        if (row["id"] == floor["condition"])
            condition = row;
    label(x, box.y + 63,
          "Best depth: " + saved["best_depth"].asString() +
              " · Floors cleared: " + saved["floors_cleared"].asString(),
          15);
    label(x, box.y + 103, "Floor " + floor["depth"].asString() + " · " + floor["region_label"].asString(),
          18);
    label(x, box.y + 137, condition["label"].asString(), 16);
    paragraph(x, box.y + 165, width, condition["description"].asString(), 14);
    label(x, box.y + 208,
          (active ? saved["progress"].asString() : "0") + "/" + floor["goal"].asString() +
              " blocks · Toughness: " + std::to_string(int(std::round(floor["dig_scale"].asDouble() * 100))) +
              "%",
          14);
    label(x, box.y + 237, "Reward: " + floor["points"].asString() + " points", 14);
    paragraph(x, box.y + 275, width,
              active ? "Clear blocks to finish this floor. Automining is unavailable. Rewards are "
                       "claimed once when cleared."
                     : "Unlocks after your first prestige. Your mine is saved while away. Each visit "
                       "starts after your best cleared floor.",
              14);
    const float bottom = box.y + box.h - 63;
    button(
        {x, bottom, width / 2 - 6, 37}, active ? "Descend" : "Enter Shaft",
        [this, active] { action("shaft", active ? "next" : "start"); },
        !ctx.resetting && (!active || game.shaftComplete()));
    if (active)
        button(
            {x + width / 2 + 6, bottom, width / 2 - 6, 37}, "Return to Mine",
            [this] {
                if (game.shaftComplete())
                    action("shaft", "leave");
                else {
                    confirmAction = "shaft_leave";
                    setPage("confirm");
                }
            },
            !ctx.resetting);
}

void UI::Impl::contracts() {
    const Json active = game.state["progression"]["contract"];
    Rect box = panel("Contracts", 660, active.isObject() ? 330 : 485);
    const float x = box.x + 24, width = box.w - 48;
    auto rewardText = [](const Json& row) {
        return row["points"].asString() + " points";
    };
    if (active.isObject()) {
        label(x, box.y + 65, active["label"].asString(), 18);
        paragraph(x, box.y + 98, width, game.contractText(active), 16);
        label(x, box.y + 155,
              compact(active["progress"].asDouble()) + " / " + compact(active["goal"].asDouble()) +
                  " · No expiry",
              15);
        label(x, box.y + 187, rewardText(active), 15);
        Json args;
        args["serial"] = active["serial"];
        button(
            {x, box.y + box.h - 66, width / 2 - 6, 38}, "Claim Rewards",
            [this, args] { apply(game.itemAction("contract_claim", args)); },
            active["progress"].asInt() >= active["goal"].asInt());
        button({x + width / 2 + 6, box.y + box.h - 66, width / 2 - 6, 38}, "Cancel Contract", [this, args] {
            confirmAction = "contract_cancel";
            confirmArgs = args;
            setPage("confirm");
        });
        return;
    }
    label(x, box.y + 64, "Choose one contract. No expiry.", 15);
    Rect list{x, box.y + 100, width, box.h - 124};
    beginScroll(list);
    float y = list.y - scroll;
    for (const auto& quote : game.contractChoices()) {
        label(x, y, quote["label"].asString(), 17);
        paragraph(x, y + 25, width - 120, game.contractText(quote), 14);
        label(x, y + 65, rewardText(quote), 14);
        button({x + width - 108, y + 12, 100, 34}, "Accept",
               [this, quote] { apply(game.itemAction("contract_accept", quote)); });
        y += 110;
    }
    endScroll(y);
}

void UI::Impl::expeditions() {
    Rect box = panel("Pet Expeditions", 690, 570);
    const float x = box.x + 24, width = box.w - 48;
    label(x, box.y + 64, "Trips advance as you mine. Recall pets any time.", 14);
    Rect list{x, box.y + 99, width, box.h - 125};
    beginScroll(list);
    float y = list.y - scroll;
    const auto& trips = game.state["progression"]["expeditions"];
    const int maximum = game.data.systems["expeditions"].get("slots", 3).asInt();
    auto placeName = [this](const Json& trip) {
        for (const auto& region : game.data.systems["regions"])
            if (region["id"] == trip["region"])
                return region["label"].asString();
        return std::string("Quarry");
    };
    auto rewardText = [this](const Json& trip) {
        std::string reward = trip["points"].asString() + " points";
        for (const auto& loot : trip["loot"])
            reward += " + " + loot.get("count", 1).asString() + " " + game.itemName(loot);
        return reward;
    };
    label(x, y, "Active trips · " + std::to_string(trips.size()) + "/" + std::to_string(maximum), 16);
    y += 32;
    if (trips.empty()) {
        label(x, y, "No pets away.", 14);
        y += 34;
    }
    for (const auto& trip : trips) {
        draw.item({x, y + 6, 42, 42}, trip["pet"], game.state["parts_prestige"].asInt(), glint);
        itemName(x + 58, y, trip["pet"], 15);
        label(x + 58, y + 23,
              placeName(trip) + " · " + compact(trip["progress"].asDouble()) + " / " +
                  compact(trip["goal"].asDouble()) + " blocks",
              13);
        label(x + 58, y + 49, rewardText(trip), 13);
        const bool ready = trip["progress"].asInt() >= trip["goal"].asInt();
        Json args;
        args["serial"] = trip["serial"];
        button({x + width - 100, y + 4, 92, 34}, ready ? "Collect" : "Recall", [this, args, ready] {
            if (ready)
                apply(game.itemAction("expedition_claim", args));
            else {
                confirmAction = "expedition_recall";
                confirmArgs = args;
                setPage("confirm");
            }
        });
        y += 90;
    }
    label(x, y + 8, "Spare pets in your inventory", 16);
    y += 42;
    bool found = false;
    for (int i = 1; i < int(game.state["inventory_slots"].size()); ++i) {
        const Json quote = game.expeditionQuote(i);
        if (quote.isNull())
            continue;
        found = true;
        draw.item({x, y + 6, 42, 42}, quote["pet"], game.state["parts_prestige"].asInt(), glint);
        itemName(x + 58, y, quote["pet"], 15);
        label(x + 58, y + 23, placeName(quote) + " · " + compact(quote["goal"].asDouble()) + " blocks", 13);
        label(x + 58, y + 47, rewardText(quote), 13);
        button(
            {x + width - 100, y + 4, 92, 34}, "Send",
            [this, quote] { apply(game.itemAction("expedition_start", quote)); },
            trips.size() < unsigned(maximum));
        y += 86;
    }
    if (!found) {
        label(x, y, "Move a spare pet into your inventory to send it.", 14);
        y += 28;
    }
    endScroll(y);
}

void UI::Impl::confirm() {
    const bool petBatch = page == "confirm" && confirmAction == "pet_food" &&
                          confirmArgs.isMember("pets") && confirmArgs["pets"].isArray();
    const bool partBatch = page == "confirm" && confirmAction == "salvage_parts" &&
                           confirmArgs.isMember("parts") && confirmArgs["parts"].isArray();
    const bool dust = page == "confirm" && confirmAction == "crystal_dust";
    const bool craft = page == "confirm" && confirmAction == "crystal_craft";
    const auto total = confirmArgs.get("count", 0).asInt64();
    const bool bulk = (dust && !confirmArgs["item"].get("identified", false).asBool() && total > 0) ||
                      (craft && total >= 8);
    Rect box = panel("Confirm", 530, petBatch || partBatch ? 440 : 330);
    std::string text;
    if (page == "prestige" || page == "confirm_prestige")
        text = "Prestige for " + compact(game.prestigeCost()) + " money? Your rank returns to A.";
    else if (page == "rebirth" || page == "confirm_rebirth")
        text = "Rebirth? Your rank and prestige reset. Your tool and items are kept.";
    else if (confirmAction == "merge_pets")
        text = "Merge five Mythical pets from your inventory into one OVERPOWERED pet?";
    else if (confirmAction == "vault_buy")
        text = "Buy another " + std::to_string(VaultPageSize) + "-slot vault page for " +
               compact(confirmArgs["cost"].asDouble()) + " money?";
    else if (confirmAction == "robot_upgrade")
        text = "Upgrade this robot by " + confirmArgs["levels"].asString() + " levels for " +
               compact(confirmArgs["rubies"].asDouble()) + " Rubys and " +
               compact(confirmArgs["sapphires"].asDouble()) + " Sapphires?";
    else if (confirmAction == "mine_select")
        text = "Enter this mine? Your current mine will be refilled.";
    else if (confirmAction == "mine_upgrade")
        text = "Buy this permanent upgrade for " + compact(confirmArgs["money"].asDouble()) + " money and " +
               confirmArgs["points"].asString() + " points?";
    else if (confirmAction == "shaft_leave")
        text = "Leave this floor? Unfinished floor progress is lost. Your mine and earned items are kept.";
    else if (confirmAction == "contract_cancel")
        text = "Cancel this contract? Its progress and unclaimed rewards will be lost.";
    else if (confirmAction == "expedition_recall")
        text = "Recall this pet? The pet returns safely, but this trip earns no rewards.";
    else if (confirmAction == "crystal_dust")
        text = "Convert this crystal into dust?";
    else if (confirmAction == "crystal_craft")
        text = "Use four dust of this tier to craft one crystal of the next tier?";
    else if (petBatch) {
        std::int64_t food = 0;
        for (const auto& row : confirmArgs["pets"])
            food += game.petFoodValue(row["item"]);
        const auto count = confirmArgs["pets"].size();
        text = "Convert " + std::to_string(count) + (count == 1 ? " pet into " : " pets into ") +
               std::to_string(food) + " pet food? This cannot be undone.";
    } else if (partBatch) {
        std::int64_t count = 0;
        for (const auto& row : confirmArgs["parts"])
            count += row["item"].get("count", 1).asInt();
        text = "Salvage " + std::to_string(count) + (count == 1 ? " part" : " parts") +
               " for tokens? This cannot be undone.";
    } else if (confirmAction == "pet_food")
        text = "Convert this pet into food?";
    else if (confirmAction == "combinemulti")
        text = "Combine four matching sell multipliers into one with double the strength and triple the "
               "duration?";
    else
        text = "Confirm this action?";
    paragraph(box.x + 28, box.y + 85, box.w - 56, text, 17);
    if (petBatch || partBatch) {
        const auto& selected = confirmArgs[petBatch ? "pets" : "parts"];
        const float x = box.x + (box.w - 9 * 40) / 2;
        for (unsigned i = 0; i < selected.size(); ++i)
            slot({x + (i % 9) * 40, box.y + 160 + (i / 9) * 40, 38, 38}, "preview", int(i),
                 selected[i]["item"]);
    } else if (bulk) {
        const int tier = confirmArgs["item"].get("tier", 1).asInt();
        std::string details = (dust ? "Dust All: " : "Craft All: ") + std::to_string(total) +
                              " matching " + (dust ? "unidentified crystals" : "dust") + " make " +
                              std::to_string(total / 4) + " T" + std::to_string(tier + 1) + " crystals.";
        if (total % 4 > 0)
            details += " " + std::to_string(total % 4) + " T" + std::to_string(tier) + " dust " +
                       (dust ? "returned." : "left over.");
        paragraph(box.x + 28, box.y + 142, box.w - 56, details, 15);
    }
    auto convert = [this, petBatch, partBatch](bool all) {
        if (page == "confirm" && confirmAction == "shaft_leave") {
            action("shaft", "leave");
            return;
        }
        if (page == "prestige" || page == "confirm_prestige" || page == "rebirth" ||
            page == "confirm_rebirth") {
            action("progression",
                   page == "rebirth" || page == "confirm_rebirth" ? "rebirth_confirm" : "prestige_confirm");
            setPage("");
            return;
        }
        Json args = confirmArgs;
        if (all)
            args["all"] = true;
        Result result = game.itemAction(confirmAction, args);
        if (result.ok) {
            if (petBatch) {
                petFoodSelection.clear();
                setPage("pet_food");
            } else if (partBatch) {
                partSelection.clear();
                setPage("bin");
            } else
                setPage(confirmAction == "pet_food" ? "pets" : "inventory");
        }
        apply(result);
    };
    const float width = (box.w - 56 - (bulk ? 28 : 14)) / (bulk ? 3 : 2);
    const float y = box.y + box.h - 85;
    button({box.x + 28, y, width, 38}, dust ? "Dust" : craft ? "Craft" : "Confirm",
           [convert] { convert(false); });
    if (bulk)
        button({box.x + 42 + width, y, width, 38}, dust ? "Dust All" : "Craft All",
               [convert] { convert(true); });
    button({box.x + box.w - 28 - width, y, width, 38}, "Cancel", [this, petBatch, partBatch] {
        std::string destination = "inventory";
        if (page == "confirm") {
            if (confirmAction == "vault_buy")
                destination = "vault";
            else if (confirmAction == "robot_upgrade")
                destination = "robots";
            else if (confirmAction == "mine_select")
                destination = "mines";
            else if (confirmAction == "mine_upgrade")
                destination = "mine_upgrades";
            else if (confirmAction == "contract_cancel")
                destination = "contracts";
            else if (confirmAction == "expedition_recall")
                destination = "expeditions";
            else if (confirmAction == "shaft_leave")
                destination = "shaft";
            else if (confirmAction == "pet_food")
                destination = petBatch ? "pet_food" : "pets";
            else if (partBatch)
                destination = "bin";
        }
        setPage(destination);
    });
}

void UI::Impl::itemSelection() {
    const bool parts = page == "bin";
    auto& selection = parts ? partSelection : petFoodSelection;
    Rect box = panel(parts ? "Bin" : "Make Pet Food", 690, 445);
    const auto& items = game.state["inventory_slots"];
    std::int64_t total = 0;
    for (auto it = selection.begin(); it != selection.end();) {
        if (it->first >= int(items.size()) || items[it->first] != it->second)
            it = selection.erase(it);
        else {
            total += parts ? it->second.get("count", 1).asInt() : game.petFoodValue(it->second);
            ++it;
        }
    }
    paragraph(box.x + 26, box.y + 69, box.w - 52,
              parts ? "Select parts to salvage for tokens. Nothing is consumed until you confirm."
                    : "Select pets to turn into food. Nothing is consumed until you confirm.");
    label(box.x + 26, box.y + 112,
          parts ? std::to_string(total) + (total == 1 ? " part selected" : " parts selected")
                : std::to_string(selection.size()) + " selected · " + std::to_string(total) + " pet food");
    button({box.x + 26, box.y + 150, 110, 34}, "Select All", [this, parts] {
        auto& selection = parts ? partSelection : petFoodSelection;
        const auto& items = game.state["inventory_slots"];
        for (int i = 1; i < int(items.size()); ++i)
            if (items[i]["id"] == (parts ? "pick_part" : "pet"))
                selection[i] = items[i];
    });
    button({box.x + 146, box.y + 150, 80, 34}, "Clear",
           [this, parts] { (parts ? partSelection : petFoodSelection).clear(); }, !selection.empty());
    button(
        {box.x + box.w - 166, box.y + 150, 140, 34}, parts ? "Salvage Selected" : "Convert Selected",
        [this, parts] {
            const auto& selection = parts ? partSelection : petFoodSelection;
            const char* key = parts ? "parts" : "pets";
            confirmAction = parts ? "salvage_parts" : "pet_food";
            confirmArgs = Json();
            confirmArgs[key] = Json(::Json::arrayValue);
            for (const auto& selected : selection) {
                Json row;
                row["index"] = selected.first;
                row["item"] = selected.second;
                confirmArgs[key].append(row);
            }
            setPage("confirm");
        },
        !selection.empty());
    inventory(box);
}

void UI::Impl::combineMultipliers() {
    const auto recipes = game.multiplierRecipes();
    const float height = std::clamp(364.f + std::max(1.f, float(recipes.size())) * 54, 420.f, 620.f);
    Rect box = panel("Combine Multipliers", 690, height);
    float x = box.x + 22, width = box.w - 44;
    const float top = paragraph(x, box.y + 62, width,
                                "Four matching sell multipliers make one with twice the strength and "
                                "three times the duration. Limit: 25x.") +
                      12;
    Rect list{x, top, width, box.y + box.h - 242 - top};
    beginScroll(list);
    float y = list.y - scroll;
    if (recipes.empty()) {
        label(x, y, "No sell multipliers in your inventory.", 15);
        y += 28;
    }
    for (const auto& recipe : recipes) {
        const double amount = recipe["amount"].asDouble(), duration = recipe["duration"].asDouble();
        const int count = recipe["count"].asInt();
        label(x, y + 2, number(amount, 2) + "x · " + clockText(duration), 15);
        label(x, y + 24, std::to_string(count) + " items", 13);
        button(
            {x + width * .46f, y + 3, width * .54f - 8, 36},
            "Combine → " + number(std::min(25.0, amount * 2), 2) + "x · " + clockText(duration * 3),
            [this, recipe] { apply(game.itemAction("combinemulti", recipe)); }, count >= 4 && amount < 25);
        y += 54;
    }
    endScroll(y);
    inventory(box);
}

void UI::Impl::saveFilterRule(const std::string& pattern) {
    const char* names[] = {"keep", "salvage", "vault", "apply"};
    Json args;
    args["pattern"] = pattern;
    args["action"] = names[filterAction];
    apply(game.itemAction("filter_rule", args));
}

void UI::Impl::filter() {
    Rect box = panel("Filter", 690, 550);
    Json settings = game.state["cleaner_settings"];
    float x = box.x + 22, y = box.y + 60, width = box.w - 44;
    const float half = (width - 12) / 2;
    button({x, y, half, 34}, settings.get("enabled", false).asBool() ? "Filter: On" : "Filter: Off",
           [this, settings]() mutable {
               settings["enabled"] = !settings.get("enabled", false).asBool();
               apply(game.itemAction("filter", settings));
           });
    const bool blacklist = settings.get("mode", "whitelist") == "blacklist";
    button({x + half + 12, y, half, 34}, blacklist ? "Mode: Blacklist" : "Mode: Whitelist",
           [this, settings, blacklist]() mutable {
               settings["mode"] = blacklist ? "whitelist" : "blacklist";
               apply(game.itemAction("filter", settings));
           });
    button(
        {x, y + 44, half, 34}, "Unlisted: " + settings.get("default_action", "salvage").asString(),
        [this, settings]() mutable {
            const std::array<std::string, 4> kinds = {"keep", "salvage", "vault", "apply"};
            const auto found = std::find(kinds.begin(), kinds.end(), settings["default_action"].asString());
            settings["default_action"] = kinds[(std::distance(kinds.begin(), found) + 1) % kinds.size()];
            apply(game.itemAction("filter", settings));
        },
        blacklist);
    const int vault = settings.get("vault", 1).asInt();
    std::string vaultName = "Vault: " + std::to_string(vault) + " · " + game.vaultName(vault - 1);
    while (draw.textWidth(vaultName, 14) > half - 18 && !vaultName.empty())
        eraseLast(vaultName);
    button({x + half + 12, y + 44, half, 34}, vaultName, [this, settings, vault]() mutable {
        settings["vault"] = vault % game.vaultPages() + 1;
        apply(game.itemAction("filter", settings));
    });
    label(x, y + 88,
          blacklist ? "Listed items stay. Other drops use the unlisted action."
                    : "Only listed drops are processed. Exact rules override families.",
          13);
    const auto& held = game.state["inventory_slots"][game.state.get("selected_hotbar_index", 0).asInt()];
    const auto heldPattern = game.filterPattern(held, filterFamily);
    draw.item({x, y + 118, 32, 32}, held);
    std::string heldName = held.isObject() ? game.itemName(held) : "Nothing held";
    while (draw.textWidth(heldName, 14) > width - 335 && !heldName.empty())
        eraseLast(heldName);
    label(x + 40, y + 126, heldName, 14);
    button({x + width - 282, y + 116, 154, 34}, filterFamily ? "Item Family" : "Exact Item",
           [this] { filterFamily = !filterFamily; });
    button(
        {x + width - 116, y + 116, 116, 34}, "Add Held",
        [this, heldPattern] {
            inputs["filter_pattern"] = heldPattern;
            saveFilterRule(heldPattern);
        },
        !heldPattern.empty());
    input({x, y + 162, width - 260, 35}, "filter_pattern", "Item id or module:*");
    const char* actions[] = {"keep", "salvage", "vault", "apply"};
    button({x + width - 248, y + 162, 125, 35}, actions[filterAction],
           [this] { filterAction = (filterAction + 1) % 4; });
    button({x + width - 112, y + 162, 112, 35}, "Save Rule",
           [this] { saveFilterRule(inputs["filter_pattern"]); });
    Rect list{x, y + 213, width, box.h - 293};
    beginScroll(list);
    float rowY = list.y - scroll;
    for (unsigned i = 0; i < settings["rules"].size(); ++i) {
        const auto& rule = settings["rules"][i];
        const auto pattern = rule["pattern"].asString();
        std::string caption = pattern;
        while (draw.textWidth(caption, 14) > width - 220 && !caption.empty())
            eraseLast(caption);
        button({x, rowY, width - 198, 32}, caption, [this, pattern, rule] {
            inputs["filter_pattern"] = pattern;
            const std::array<std::string, 4> kinds = {"keep", "salvage", "vault", "apply"};
            filterAction = int(std::distance(
                kinds.begin(), std::find(kinds.begin(), kinds.end(), rule["action"].asString())));
        });
        button(
            {x + width - 186, rowY, 92, 32}, rule["action"].asString(),
            [this, i, settings]() mutable {
                const std::array<std::string, 4> kinds = {"keep", "salvage", "vault", "apply"};
                auto& action = settings["rules"][i]["action"];
                const auto found = std::find(kinds.begin(), kinds.end(), action.asString());
                action = kinds[(std::distance(kinds.begin(), found) + 1) % kinds.size()];
                apply(game.itemAction("filter", settings));
            },
            !blacklist);
        button({x + width - 82, rowY, 78, 32}, "Remove", [this, i, settings]() mutable {
            Json removed;
            settings["rules"].removeIndex(i, &removed);
            apply(game.itemAction("filter", settings));
        });
        rowY += 43;
    }
    if (settings["rules"].empty())
        label(x + 4, rowY + 6, "No rules. Add a held item or enter its id.", 14);
    endScroll(rowY);
}

void UI::Impl::pointshop() {
    Rect box = panel("Point Shop", 690, 560);
    label(box.x + 22, box.y + 58, "Points: " + compact(game.state.get("points", 0).asDouble()));
    Rect list{box.x + 22, box.y + 94, box.w - 44, box.h - 125};
    beginScroll(list);
    float y = list.y - scroll;
    for (const auto& product : game.pointShopProducts()) {
        draw.item({list.x + 4, y, 34, 34}, product["item"]);
        label(list.x + 50, y + 9, product["name"].asString());
        const std::string key = product["key"].asString();
        button({list.x + list.w - 148, y, 130, 35}, std::to_string(product["price"].asInt()) + " points",
               [this, key] {
                   Json args;
                   args["key"] = key;
                   apply(game.itemAction("buy_point", args));
               });
        y += 47;
    }
    endScroll(y);
}

void UI::Impl::automine() {
    Rect box = panel("Autominer", 560, 380);
    float x = box.x + 26, y = box.y + 75, width = box.w - 52;
    label(x, y, "Time: " + clockText(game.state.get("autominer_time", 0).asDouble()));
    int speed = game.state.get("autominer_bps", 3).asInt();
    int unlocked = game.state.get("autominer_unlocked_bps", 3).asInt();
    label(x, y + 38, "Speed: " + std::to_string(speed) + " blocks / second");
    label(x, y + 65, "Time cost: " + std::to_string(game.autominerTickCost()) + "s / second", 14);
    button(
        {x, y + 99, 42, 34}, "−",
        [this, speed] {
            Json args;
            args["value"] = speed - 1;
            apply(game.itemAction("autominer_bps", args));
        },
        speed > 1);
    button(
        {x + 52, y + 99, 42, 34}, "+",
        [this, speed] {
            Json args;
            args["value"] = speed + 1;
            apply(game.itemAction("autominer_bps", args));
        },
        speed < unlocked);
    const Json& cfg = game.data.systems["autominer"];
    double cost = cfg.get("upgrade_base_cost", 1000000000).asDouble() +
                  (unlocked - 1) * cfg.get("upgrade_step", 250).asDouble();
    button(
        {x + 112, y + 99, width - 112, 34}, unlocked < 10 ? "Upgrade: " + compact(cost) : "10 BPS unlocked",
        [this] { apply(game.itemAction("autominer_upgrade")); }, unlocked < cfg.get("max_bps", 10).asInt());
    bool active = game.state.get("autominer_active", false).asBool();
    button({x, y + 153, width, 38}, active ? "Stop Autominer" : "Start Autominer",
           [this, active] { apply(game.itemAction(active ? "autominer_stop" : "autominer_start")); });
    paragraph(x, y + 210, width, "Hold your pickaxe. Manual mining is disabled while automining.", 14);
}

void UI::Impl::rename() {
    Rect box = panel("Rename", 620, 350);
    paragraph(box.x + 26, box.y + 69, box.w - 52,
              "Paste a Minecraft gradient, or use &#rrggbb and &l for bold. Up to 40 characters. "
              "Confirming spends one Rename Token.",
              15);
    input({box.x + 26, box.y + 143, box.w - 52, 38}, "rename");
    button({box.x + 26, box.y + 212, (box.w - 64) / 2, 38}, "Preview", [this] {
        Json args;
        args["text"] = inputs["rename"];
        apply(game.itemAction("rename_preview", args));
    });
    button({box.x + 38 + (box.w - 64) / 2, box.y + 212, (box.w - 64) / 2, 38}, "Confirm", [this] {
        Json args;
        args["text"] = inputs["rename"];
        auto result = game.itemAction("rename", args);
        if (result.ok)
            setPage("");
        apply(result);
    });
}

void UI::Impl::searchHelp() {
    inputs["help_search"] = inputs["help_search"].substr(0, 128);
    helpQuery = inputs["help_search"];
    std::transform(helpQuery.begin(), helpQuery.end(), helpQuery.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    game.state["_help_search"] = inputs["help_search"];
    scroll = helpListScroll = 0;
}

void UI::Impl::help() {
    Rect box = panel("Help", 730, 570);
    float x = box.x + 22, width = box.w - 44;
    const char* tabs[] = {"commands", "guide"};
    const char* labels[] = {"Commands", "Guide"};
    for (int i = 0; i < 2; ++i) {
        const std::string tab = tabs[i];
        Rect tabBox{x + i * 126, box.y + 58, 118, 31};
        button(tabBox, labels[i], [this, tab] {
            helpTab = tab;
            helpPage.clear();
            scroll = helpListScroll = 0;
        });
        if (helpTab == tab)
            draw.border(tabBox, foreground, 2);
    }
    input({x, box.y + 101, width - 76, 32}, "help_search",
          helpTab == "commands" ? "Search commands" : "Search guide");
    button({x + width - 68, box.y + 101, 68, 32}, "Clear", [this] {
        inputs["help_search"].clear();
        searchHelp();
        field = "help_search";
        SDL_StartTextInput();
    });
    struct HelpPage {
        std::string id, title, section;
        Json body;
    };
    std::vector<HelpPage> pages;
    auto matches = [this](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        std::istringstream query(helpQuery);
        std::string word;
        while (query >> word)
            if (text.find(word) == std::string::npos)
                return false;
        return true;
    };
    if (helpTab == "guide") {
        for (const auto& section : game.data.help["sections"])
            for (const auto& entry : section["pages"])
                if (matches(section["title"].asString() + " " + jsonText(entry)))
                    pages.push_back({"wiki:" + entry["id"].asString(), entry["title"].asString(),
                                     section["title"].asString(), entry["body"]});
    } else {
        for (const auto& entry : game.data.help["commands"]) {
            const bool dev = entry.get("devmode", false).asBool();
            if ((dev && !game.state.get("devmode", false).asBool()) || !matches(jsonText(entry)))
                continue;
            const std::string category = dev ? "Devmode" : entry.get("category", "General").asString();
            auto found = std::find_if(pages.begin(), pages.end(),
                                      [&](const HelpPage& item) { return item.id == category; });
            if (found == pages.end()) {
                pages.push_back({category, category, "Commands", Json(::Json::arrayValue)});
                found = pages.end() - 1;
            }
            found->body.append(entry);
        }
    }
    int selected = 0;
    for (unsigned i = 0; i < pages.size(); ++i)
        if (pages[i].id == helpPage)
            selected = int(i);
    if (!pages.empty())
        helpPage = pages[selected].id;
    helpListBox = {x, box.y + 145, std::min(218.f, width * .34f), box.h - 167};
    draw.rect(helpListBox, dim(background, .65f));
    draw.border(helpListBox, outline);
    const float articleScroll = scroll;
    scroll = helpListScroll;
    beginScroll({helpListBox.x + 2, helpListBox.y + 2, helpListBox.w - 4, helpListBox.h - 4});
    float y = clipping.y - scroll;
    std::string section;
    for (unsigned i = 0; i < pages.size(); ++i) {
        if (helpTab == "guide" && pages[i].section != section) {
            section = pages[i].section;
            label(clipping.x + 7, y + 6, section, 12);
            y += 28;
        }
        Rect row{clipping.x, y, clipping.w - 5, 29};
        if (int(i) == selected || row.contains(mouseX, mouseY))
            draw.rect(row, dim(background, int(i) == selected ? 1.7f : 1.3f));
        label(row.x + 7, y + 5, pages[i].title, 15);
        const auto id = pages[i].id;
        region(row, [this, id] {
            helpPage = id;
            scroll = 0;
        });
        y += 29;
    }
    endScroll(y);
    helpListScroll = scroll;
    helpListMaximum = scrollMaximum;
    scroll = articleScroll;
    const float articleX = helpListBox.x + helpListBox.w + 14;
    Rect article{articleX, helpListBox.y, x + width - articleX, helpListBox.h};
    beginScroll(article);
    y = article.y - scroll;
    if (pages.empty()) {
        label(article.x, y, "No matches", 18);
        y = paragraph(article.x, y + 32, article.w - 8,
                      helpTab == "commands" ? "Try a command, alias or category. For topics, open Guide."
                                            : "Try an item name or open Commands.");
    } else {
        const auto& entry = pages[selected];
        label(article.x, y, entry.section, 14);
        y = paragraph(article.x, y + 25, article.w - 8, entry.title, 18) + 10;
        for (const auto& line : entry.body) {
            if (helpTab == "guide") {
                y = paragraph(article.x, y, article.w - 8, line.asString(), 15) + 12;
                continue;
            }
            y = paragraph(article.x, y, article.w - 8,
                          line.get("usage", "/" + line["command"].asString()).asString(), 15) +
                5;
            y = paragraph(article.x, y, article.w - 8, line["description"].asString(), 14) + 5;
            std::string aliases;
            for (const auto& alias : line["aliases"])
                aliases += (aliases.empty() ? "Aliases: /" : ", /") + alias.asString();
            if (!aliases.empty())
                y = paragraph(article.x, y, article.w - 8, aliases, 12) + 5;
            y += 15;
        }
    }
    endScroll(y);
}

void UI::Impl::information() {
    std::string title = page == "help"      ? "Help"
                        : page == "boosts"  ? "Boosts"
                        : page == "rewards" ? "Rewards"
                                            : "Help";
    Rect box = panel(title, 730, 570);
    Rect list{box.x + 22, box.y + 61, box.w - 44, box.h - 89};
    beginScroll(list);
    float y = list.y - scroll;
    if (page == "boosts") {
        const char* keys[] = {"money", "token", "xp"};
        const char* names[] = {"Money", "Tokens", "EXP"};
        for (int i = 0; i < 3; ++i) {
            currencyLabel(list.x, y, std::string(names[i]) + ": " + number(game.boost(keys[i]), 2) + "x", 18);
            y += 33;
            const auto& robot = game.state["robot_slots"][i == 1 ? 2 : i == 2 ? 1 : 0];
            if (robot.isObject()) {
                const bool powered = robot.get("fuel", 0).asInt() >= game.robotFuelCost(robot);
                label(list.x + 15, y,
                      "Robot: " + (powered ? number(game.robotMultiplier(robot), 3) + "x" : "Out of fuel"),
                      14);
                y += 28;
            }
            for (const auto& boost : game.state["boosts"][keys[i]]) {
                if (!boost.isObject())
                    continue;
                label(list.x + 15, y,
                      number(boost.get("amount", 1).asDouble(), 2) + "x    " +
                          clockText(boost.get("remaining", 0).asDouble()));
                y += 28;
            }
            y += 18;
        }
        for (const auto& value : game.data.systems["endgame"]) {
            const auto id = value.asString();
            const double remaining = game.state["endgame"][id].get("remaining", 0).asDouble();
            if (remaining <= 0 || game.endgameMultiplier(id) <= 1)
                continue;
            label(list.x, y, game.data.enchants["defs"][id]["label"].asString() + ": " + clockText(remaining),
                  15);
            y += 28;
        }
        const auto& resonance = game.crystalResonance();
        if (resonance.isObject())
            y = paragraph(list.x, y, list.w, "Resonance: " + resonance["description"].asString(), 14) + 16;
        const auto& mutation = game.petMutation(game.state["pet_data"]["active_pet"]);
        if (mutation.isObject())
            y = paragraph(list.x, y, list.w,
                          mutation["label"].asString() + ": " + mutation["description"].asString(), 14) +
                16;
        button({list.x, y, 230, 36}, "Return Sell Multiplier",
               [this] { apply(game.itemAction("endmulti")); });
        y += 48;
    } else if (page == "rewards") {
        for (const auto& milestone : game.data.systems["reward_milestones"]) {
            bool achieved = game.state.get("blocks_mined", 0).asDouble() >= milestone["blocks"].asDouble();
            currencyLabel(list.x, y,
                          (achieved ? "✓ " : "  ") + compact(milestone["blocks"].asDouble()) +
                              " blocks:  " + compact(milestone.get("tokens", 0).asDouble()) + " tokens / " +
                              compact(milestone.get("money", 0).asDouble()) + " money",
                          15);
            y += 34;
        }
    }
    endScroll(y);
}

} // namespace prison
