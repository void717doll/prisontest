#include "ui_internal.hpp"
#include <iomanip>
#include <sstream>

namespace prison {
Color UI::Impl::dim(Color color, float amount) {
    color.r *= amount;
    color.g *= amount;
    color.b *= amount;
    return color;
}

std::string UI::Impl::number(double value, int precision) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

std::string UI::Impl::blockCount(double value) {
    const auto count = static_cast<std::uint64_t>(std::isfinite(value) ? std::clamp(value, 0.0, 9e15) : 0);
    std::string text = std::to_string(count);
    for (int at = int(text.size()) - 3; at > 0; at -= 3)
        text.insert(at, ",");
    return text;
}

std::string UI::Impl::clockText(double seconds) {
    auto value = static_cast<std::int64_t>(std::clamp(seconds, 0.0, 9e15));
    return std::to_string(value / 3600) + "h " + std::to_string(value / 60 % 60) + "m " +
           std::to_string(value % 60) + "s";
}

UI::Impl::Impl(Game& g, Renderer& r, UiContext& c) : game(g), draw(r), ctx(c) {
    inputs["world_name"] = "Your Mine";
    inputs["filter_pattern"] = "";
    inputs["rename"] = "";
    inputs["enchant_amount"] = "1";
}

void UI::Impl::action(const std::string& kind, const std::string& value) {
    ctx.actions.push_back({kind, value});
}

void UI::Impl::settingsChanged() {
    game.state["settings"]["show_max_labels"] = ctx.settings["show_max_labels"];
    game.state["message_settings"]["show_max_enchant_label"] = ctx.settings["show_max_labels"];
    action("settings");
}

void UI::Impl::setPage(const std::string& name) {
    if ((name == "statistics" || name == "stats" || name == "records") && page != "statistics") {
        statisticsReturn = page == "pause" || page == "more" ? page : "";
        statisticsCategory = "General";
    }
    if (name == "records")
        statisticsCategory = "Records";
    if (name != "pet_food" && name != "confirm")
        petFoodSelection.clear();
    if (name != "bin" && name != "confirm")
        partSelection.clear();
    if (ctx.playing)
        game.returnStoredItems();
    if (page == "tutorial" && name.empty() && game.state["tutorial"].get("status", "") == "new") {
        game.tutorialAction("skip");
        action("save");
    }
    page = name;
    if (page == "stats" || page == "records")
        page = "statistics";
    if (page.empty() && ctx.playing && game.state["tutorial"].get("status", "") == "new")
        page = "tutorial";
    if (page == "inventory")
        game.tutorialEvent("inventory");
    if (page == "pickaxe" || page == "tool")
        page = "pick";
    if (page == "enchant")
        page = "enchants";
    if (page == "pv")
        page = "vault";
    if (page.rfind("vault:", 0) == 0) {
        try {
            vaultIndex = std::stoi(page.substr(6));
        } catch (const std::exception&) {
            vaultIndex = 0;
        }
        page = "vault";
    }
    if (page == "vault") {
        vaultIndex = std::clamp(vaultIndex, 0, game.vaultPages() - 1);
        inputs["vault_name"] = game.vaultName(vaultIndex);
    }
    if (page == "trash" || page == "salvage")
        page = "bin";
    if (page == "autominer" || page == "am")
        page = "automine";
    if (page == "petmerge")
        page = "pets";
    if (page == "controls")
        page = "settings";
    if (page == "prestige_confirm")
        page = "confirm_prestige";
    if (page == "rebirth_confirm")
        page = "confirm_rebirth";
    if (page.empty() && !ctx.playing)
        page = "title";
    field.clear();
    listening.clear();
    sliderKey.clear();
    selectedContainer.clear();
    selectedIndex = -1;
    scroll = tooltipScroll = 0;
    buttons.clear();
    slots.clear();
    if (page.rfind("confirm:", 0) == 0) {
        const auto split = page.find(':', 8);
        confirmAction = page.substr(8, split - 8);
        confirmArgs = Json();
        if (split != std::string::npos) {
            try {
                confirmArgs["index"] = std::stoi(page.substr(split + 1));
            } catch (const std::exception&) {
                confirmArgs["index"] = -1;
            }
            const int index = confirmArgs["index"].asInt();
            if (index >= 0 && index < int(game.state["inventory_slots"].size()))
                confirmArgs["item"] = game.state["inventory_slots"][index];
            if (confirmAction == "crystal_dust" || confirmAction == "crystal_craft") {
                const auto quote = game.crystalConversionQuote(index);
                if (quote.isObject())
                    confirmArgs = quote;
            }
        }
        page = "confirm";
    }
    if (page == "chat") {
        field = "chat";
        inputs[field].clear();
        historyIndex = int(history.size());
    }
    if (page == "rename")
        inputs["rename"] = game.state.get("_rename_old", "Prison Tool").asString();
    if (page == "equipment_sets")
        for (int i = 0; i < 3; ++i)
            inputs["set_name_" + std::to_string(i)] = game.state["equipment_sets"][i]["name"].asString();
    if (page == "help") {
        inputs["help_search"] = game.state.get("_help_search", "").asString();
        if (!inputs["help_search"].empty() && inputs["help_search"][0] == '/')
            helpTab = "commands";
        searchHelp();
        helpPage.clear();
        helpListScroll = 0;
        field = "help_search";
    }
    if (page == "theme")
        for (const auto* key : {"background", "foreground", "border", "glint"})
            inputs[key] = ctx.settings[key].asString();
    if (page == "test" || page == "devtools")
        setupDevFields();
    if (page == "chat" || page == "help")
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void UI::Impl::apply(const Result& result) {
    if (result.ok && result.menu == "mine_upgrades")
        action("mine_options");
    if (!result.message.empty()) {
        ctx.say(result.message, result.ok ? Color{.55f, 1, .55f, 1} : Color{1, .6f, .5f, 1});
    }
    if (result.menu == "tutorial:close") {
        action("save");
        setPage("");
    } else if (result.menu == "rename:preview") {
        ctx.say(namedText(game.state["_rename_pending"].asString(), foreground));
    } else if (result.menu == "test:mine") {
        setPage("test");
        devTab = "mine";
    } else if (result.menu == "test:mine:apply") {
        setPage("");
    } else if (result.menu == "guardian:summon") {
        action("guardian");
    } else if (result.menu == "autominer:start" || result.menu == "autominer:stop") {
        if (result.menu == "autominer:start")
            action("mine_entry");
        setPage("");
    } else if (result.menu == "clear_chat") {
        ctx.messages.clear();
        setPage("");
    } else if (result.menu.rfind("stress:", 0) == 0) {
        action("stress", result.menu.substr(7));
        setPage("");
    } else if (!result.menu.empty())
        setPage(result.menu);
    if (result.resetMine)
        action("reset");
}

void UI::Impl::command(const std::string& value) {
    action("command", value);
}

bool UI::Impl::visible(Rect box) const {
    return !clipped || (box.y >= clipping.y && box.y + box.h <= clipping.y + clipping.h);
}

void UI::Impl::region(Rect box, std::function<void()> callback) {
    if (visible(box))
        buttons.push_back({box, std::move(callback)});
}

void UI::Impl::label(float x, float y, const std::string& value, int size) {
    draw.text(x, y, value, foreground, size);
}

void UI::Impl::loreLine(float x, float y, const LoreLine& line, int size) {
    for (const auto& span : line) {
        draw.text(x, y, span.text, span.color, size, span.style);
        x += draw.textWidth(span.text, size, span.style);
    }
}

void UI::Impl::currencyLabel(float x, float y, const std::string& text, int size) {
    loreLine(x, y, currencyText(game.data, text, foreground), size);
}

float UI::Impl::loreWidth(const LoreLine& line, int size) {
    float width = 0;
    for (const auto& span : line)
        width += draw.textWidth(span.text, size, span.style);
    return width;
}

void UI::Impl::itemName(float x, float y, const Json& item, int size) {
    loreLine(x, y, itemTitle(game, item, foreground), size);
}

void UI::Impl::centered(float y, const std::string& text, int size) {
    label((draw.width() - draw.textWidth(text, size)) * 0.5f, y, text, size);
}

void UI::Impl::button(Rect box, const std::string& text, std::function<void()> callback, bool enabled) {
    const bool hover = enabled && box.contains(mouseX, mouseY) && visible(box);
    draw.rect(box, dim(background, hover ? 1.55f : 1.2f));
    draw.border(box, enabled ? outline : dim(outline, 0.6f));
    draw.text(box.x + (box.w - draw.textWidth(text, 15)) * 0.5f, box.y + (box.h - 18) * 0.5f, text,
              enabled ? foreground : dim(foreground, 0.5f), 15);
    if (enabled)
        region(box, std::move(callback));
}

void UI::Impl::pageButton(Rect box, const std::string& text, const std::string& destination) {
    button(box, text, [this, destination] { setPage(destination); });
}

void UI::Impl::toggle(Rect box, const std::string& title, const std::string& key) {
    const bool enabled = ctx.settings.get(key, false).asBool();
    button(box, title + ": " + (enabled ? "On" : "Off"), [this, key] {
        ctx.settings[key] = !ctx.settings.get(key, false).asBool();
        settingsChanged();
    });
}

void UI::Impl::slider(Rect box, const std::string& title, const std::string& key, double minimum,
                      double maximum, const std::string& suffix, double displayScale) {
    double value = std::clamp(ctx.settings.get(key, minimum).asDouble(), minimum, maximum);
    draw.rect(box, dim(background, 0.8f));
    draw.border(box, outline);
    float width = float((value - minimum) / (maximum - minimum)) * (box.w - 8);
    draw.rect({box.x + 4, box.y + box.h - 6, width, 2}, foreground);
    std::string text = title + ": " + number(value * displayScale) + suffix;
    draw.text(box.x + (box.w - draw.textWidth(text, 15)) * 0.5f, box.y + 7, text, foreground, 15);
    region(box, [this, box, key, minimum, maximum] {
        sliderKey = key;
        sliderBox = box;
        sliderMin = minimum;
        sliderMax = maximum;
        updateSlider();
    });
}

void UI::Impl::input(Rect box, const std::string& key, const std::string& placeholder) {
    draw.rect(box, dim(background, 0.45f));
    draw.border(box, field == key ? foreground : outline);
    const std::string value = inputs[key].empty() ? placeholder : inputs[key];
    draw.clip({box.x + 5, box.y, box.w - 10, box.h});
    draw.text(box.x + 7, box.y + 7, value + (field == key && SDL_GetTicks() % 1000 < 500 ? "|" : ""),
              inputs[key].empty() ? dim(foreground, 0.5f) : foreground, 15);
    draw.unclip();
    if (clipped)
        draw.clip(clipping);
    region(box, [this, key] {
        field = key;
        SDL_StartTextInput();
    });
}

float UI::Impl::paragraph(float x, float y, float width, const std::string& text, int size) {
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        if (!line.empty() && draw.textWidth(line + " " + word, size) > width) {
            label(x, y, line, size);
            y += size + 6;
            line.clear();
        }
        if (!line.empty())
            line += ' ';
        line += word;
    }
    if (!line.empty()) {
        label(x, y, line, size);
        y += size + 6;
    }
    return y;
}

Rect UI::Impl::panel(const std::string& title, float width, float height) {
    width = std::min(width, draw.width() - 28);
    height = std::min(height, draw.height() - 32);
    Rect box{(draw.width() - width) * 0.5f, (draw.height() - height) * 0.5f, width, height};
    draw.rect({0, 0, draw.width(), draw.height()}, {0, 0, 0, 0.4f});
    draw.rect({box.x + 4, box.y + 4, box.w, box.h}, {0, 0, 0, 0.28f});
    draw.rect(box, background);
    draw.border(box, outline);
    label(box.x + 18, box.y + 17, title, 19);
    button({box.x + box.w - 84, box.y + 10, 70, 32}, "Done", [this] {
        if (page == "statistics")
            setPage(statisticsReturn);
        else if (page == "itemviewer" || page == "theme")
            setPage("settings");
        else if (page == "pet_food")
            setPage("pets");
        else if (page == "confirm" && confirmAction == "pet_food")
            setPage(confirmArgs.get("pets", Json()).isArray() ? "pet_food" : "pets");
        else if (page == "confirm" && confirmAction == "salvage_parts")
            setPage("bin");
        else
            setPage(ctx.playing ? "" : "title");
    });
    return box;
}

void UI::Impl::beginScroll(Rect box) {
    clipping = box;
    clipped = true;
    draw.clip(box);
}

void UI::Impl::endScroll(float contentBottom) {
    draw.unclip();
    clipped = false;
    scrollMaximum = std::max(0.f, contentBottom + scroll - clipping.y - clipping.h);
    scroll = std::clamp(scroll, 0.f, scrollMaximum);
    if (scrollMaximum > 0) {
        float height = std::max(20.f, clipping.h * clipping.h / (clipping.h + scrollMaximum));
        float y = clipping.y + (clipping.h - height) * scroll / scrollMaximum;
        draw.rect({clipping.x + clipping.w - 3, clipping.y, 3, clipping.h}, dim(background, 0.6f));
        draw.rect({clipping.x + clipping.w - 3, y, 3, height}, outline);
    }
}

Color UI::Impl::itemColor(const Json& item) const {
    return itemNameColor(game, item, foreground);
}

void UI::Impl::slot(Rect box, const std::string& container, int index, const Json& value, bool active) {
    draw.rect(box, dim(background, 0.35f));
    draw.border(box, dim(outline, 0.6f));
    bool selected = selectedContainer == container && selectedIndex == index;
    if (page == "pet_food" && container == "inventory")
        selected = petFoodSelection.count(index) != 0;
    else if (page == "bin" && container == "inventory")
        selected = partSelection.count(index) != 0;
    if (active && (selected || box.contains(mouseX, mouseY)))
        draw.border(box, foreground, selected ? 2 : 1);
    draw.item({box.x + 4, box.y + 4, box.w - 8, box.h - 8}, value,
              game.state.get("parts_prestige", 0).asInt(), glint);
    if (active && visible(box))
        slots.push_back({box, container, index, value});
}

void UI::Impl::inventory(Rect box) {
    const float size = 43;
    const float x = box.x + (box.w - size * 9) * 0.5f;
    const float y = box.y + box.h - 4 * size - 32;
    label(x, y - 26, "Inventory", 15);
    const Json& items = game.state["inventory_slots"];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 9; ++col) {
            int index = row == 3 ? col : 9 + row * 9 + col;
            bool active = true;
            if (page == "pet_food")
                active = index > 0 && items[index]["id"] == "pet";
            else if (page == "bin")
                active = index > 0 && items[index]["id"] == "pick_part";
            slot({x + col * size, y + row * size + (row == 3 ? 5.f : 0.f), size - 2, size - 2}, "inventory",
                 index, items[index], active);
        }
}

void UI::Impl::paint() {
    if (ctx.playing && game.state.get("_tutorial_finished", false) == true) {
        ctx.say("Tutorial complete. Use /help for the rest of the game.");
        game.state.removeMember("_tutorial_finished");
    }
    background = hexColor(ctx.settings.get("background", "#242428").asString());
    foreground = hexColor(ctx.settings.get("foreground", "#dddddf").asString());
    outline = hexColor(ctx.settings.get("border", "#68686f").asString());
    glint = hexColor(ctx.settings.get("glint", "#b56cff").asString());
    buttons.clear();
    hints.clear();
    slots.clear();
    clipped = false;
    scrollMaximum = 0;
    hud();
    if (page == "title")
        title();
    else if (page == "credits")
        credits();
    else if (page == "worlds" || page == "create")
        worlds();
    else if (page == "import")
        importWorld();
    else if (page == "help")
        help();
    else if (page == "pause")
        pause();
    else if (page == "statistics")
        statistics();
    else if (page == "tutorial")
        tutorial();
    else if (page == "settings" || page == "itemviewer" || page == "theme")
        settings();
    else if (page == "bindings")
        bindings();
    else if (page == "pick")
        tool();
    else if (page == "enchants")
        enchants();
    else if (page == "enchant_detail")
        enchantDetail();
    else if (page == "crystals" || page == "parts")
        equipment();
    else if (page == "resonance")
        resonance();
    else if (page == "guardian")
        guardian();
    else if (page == "pets")
        pets();
    else if (page == "robots")
        robots();
    else if (page == "inventory" || page == "vault" || page.rfind("pouch:", 0) == 0)
        storage();
    else if (page == "more")
        more();
    else if (page == "mines")
        mines();
    else if (page == "quarry")
        quarry();
    else if (page == "mine_upgrades")
        mineUpgrades();
    else if (page == "equipment_sets")
        equipmentSets();
    else if (page == "shaft")
        shaft();
    else if (page == "contracts")
        contracts();
    else if (page == "expeditions")
        expeditions();
    else if (page == "combinemulti")
        combineMultipliers();
    else if (page == "bin" || page == "pet_food")
        itemSelection();
    else if (page == "filter")
        filter();
    else if (page == "pointshop")
        pointshop();
    else if (page == "automine")
        automine();
    else if (page == "confirm" || page == "prestige" || page == "confirm_prestige" || page == "rebirth" ||
             page == "confirm_rebirth")
        confirm();
    else if (page == "rename")
        rename();
    else if (page == "test" || page == "devtools")
        devtools();
    else if (page == "chat")
        input({12, draw.height() - 52, draw.width() - 24, 35}, "chat");
    else if (!page.empty() && page != "hud")
        information();
    tooltip();
    if (leftDown && selectedIndex >= 0 && std::abs(mouseX - downX) + std::abs(mouseY - downY) > 5) {
        for (const auto& value : slots)
            if (value.container == selectedContainer && value.index == selectedIndex) {
                draw.item({mouseX - 18, mouseY - 18, 36, 36}, value.item,
                          game.state.get("parts_prestige", 0).asInt(), glint);
                break;
            }
    }
}

UI::UI(Game& game, Renderer& renderer, UiContext& context)
    : p(std::make_unique<Impl>(game, renderer, context)) {}
UI::~UI() = default;
void UI::event(const SDL_Event& event) {
    p->event(event);
}
void UI::draw() {
    p->paint();
}
bool UI::capturesInput() const {
    return !p->page.empty();
}
std::string UI::menu() const {
    return p->page;
}
void UI::open(const std::string& page) {
    p->setPage(page);
}
void UI::apply(const Result& result) {
    Result uiResult = result;
    uiResult.resetMine = false;
    p->apply(uiResult);
}
} // namespace prison
