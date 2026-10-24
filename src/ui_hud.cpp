#include "prison/guardians.hpp"
#include "ui_internal.hpp"
#include <array>

namespace prison {
Rect UI::Impl::hudRect(const std::string& name, Rect fallback) {
    const Json& position = ctx.settings["hud"][name];
    if (position.isObject()) {
        if (position["x"].isNumeric() && std::isfinite(position["x"].asDouble()))
            fallback.x = std::clamp(position["x"].asDouble(), 0.0, 1.0) * draw.width();
        if (position["y"].isNumeric() && std::isfinite(position["y"].asDouble()))
            fallback.y = std::clamp(position["y"].asDouble(), 0.0, 1.0) * draw.height();
    }
    fallback.x = std::clamp(fallback.x, 0.f, std::max(0.f, draw.width() - fallback.w));
    fallback.y = std::clamp(fallback.y, 0.f, std::max(0.f, draw.height() - fallback.h));
    hudBoxes.push_back({name, fallback});
    return fallback;
}

Rect UI::Impl::bossBars() {
    if (!ctx.playing || !page.empty())
        return {};
    struct Bar {
        std::string title;
        double fraction;
        Color color;
    };
    std::vector<Bar> bars;
    const double autominerTime = game.state.get("autominer_time", 0).asDouble();
    if (game.state.get("autominer_active", false).asBool() && autominerTime > 0) {
        const double duration = game.state.get("_autominer_duration", autominerTime).asDouble();
        bars.push_back({"Autominer · " + clockText(std::ceil(autominerTime / game.autominerTickCost())) +
                            " left",
                        autominerTime / std::max(1.0, duration), hexColor("#c9705f")});
    }
    const char* kinds[] = {"money", "token", "xp"};
    const char* labels[] = {"Sell", "Token", "XP"};
    const Color colors[] = {{.3f, .9f, .3f, 1}, currencyColor(game.data, "tokens"), {.8f, .7f, .95f, 1}};
    for (int i = 0; i < 3; ++i) {
        const auto multiplier = game.activeMultiplier(kinds[i]);
        if (multiplier.amount <= 1 || multiplier.remaining <= 0)
            continue;
        bars.push_back({number(multiplier.amount, 2) + "x " + labels[i] + " · " +
                            clockText(std::ceil(multiplier.remaining)),
                        multiplier.remaining / multiplier.duration, colors[i]});
    }
    const auto& guardian = game.state["guardian"];
    if (guardian["active"].asBool())
        bars.push_back({"Stone Guardian · " + guardianWeakName(guardian) + " · " +
                            clockText(std::ceil(guardian["remaining"].asDouble())),
                        guardian["health"].asDouble() / std::max(1, guardian["maximum"].asInt()),
                        hexColor(game.data.systems["guardians"]["color"].asString())});
    for (const auto& entry : game.data.systems["endgame"]) {
        const auto id = entry.asString();
        const double remaining = game.state["endgame"][id].get("remaining", 0).asDouble();
        if (remaining <= 0 || game.endgameMultiplier(id) <= 1)
            continue;
        const auto& def = game.data.enchants["defs"][id];
        bars.push_back({def["label"].asString() + " · " + clockText(std::ceil(remaining)),
                        remaining / std::max(1.0, game.data.enchants["effects"][id]["duration"].asDouble()),
                        hexColor(def["color"].asString())});
    }
    if (bars.empty())
        return {};

    float width = std::min(364.f, draw.width() - 32);
    float x = std::floor((draw.width() - width) / 2);
    for (const auto& [name, box] : hudBoxes) {
        if (name != "scoreboard" || box.x >= x + width || box.x + box.w <= x)
            continue;
        const float left = box.x - 24, right = draw.width() - box.x - box.w - 24;
        if (std::max(left, right) >= 240) {
            width = std::min(width, std::max(left, right));
            x = left >= right ? (box.x - width) / 2 : box.x + box.w + (right + 24 - width) / 2;
        }
    }
    const float spacing = std::min(39.f, (draw.height() * .5f - 20) / bars.size());
    const bool compact = spacing < 34;
    const int font = compact ? 12 : 13;
    Rect bounds{x, 9, width, 0};
    float y = 9;
    for (const auto& bar : bars) {
        const float textX = x + (width - draw.textWidth(bar.title, font)) / 2;
        draw.text(textX + 1, y + 1, bar.title, {0, 0, 0, 1}, font);
        draw.text(textX, y, bar.title, foreground, font);
        Rect track{x, y + (compact ? 15 : 18), width, compact ? 7.f : 10.f};
        draw.rect(track, {0.06f, 0.025f, .08f, .95f});
        draw.rect({x + 2, track.y + 2, float((width - 4) * std::clamp(bar.fraction, 0.0, 1.0)), track.h - 4},
                  bar.color);
        draw.border(track, dim(bar.color, .45f), 1);
        for (int i = 1; i < 20; ++i)
            draw.rect({x + 2 + (width - 4) * i / 20, track.y + 2, 1, track.h - 4}, {0, 0, 0, .4f});
        bounds.h = track.y + track.h - bounds.y;
        y += spacing;
    }
    return bounds;
}

void UI::Impl::hud() {
    if (!ctx.playing)
        return;
    hudBoxes.clear();
    const bool editing = page == "hud";
    const float width = draw.width(), height = draw.height();
    if (page.empty()) {
        draw.rect({width / 2 - 5, height / 2, 11, 1}, foreground);
        draw.rect({width / 2, height / 2 - 5, 1, 11}, foreground);
    }
    const bool scoreboard = game.state["settings"].get("show_scoreboard", true).asBool();
    if (scoreboard || editing) {
        const auto multiplier = game.activeMultiplier("money");
        const std::string multi = multiplier.remaining > 0 ? clockText(multiplier.remaining) + " (" +
                                                                 number(multiplier.amount, 2) + "x)"
                                                           : "None";
        const double bankedTime = game.state.get("autominer_time", 0).asDouble();
        const std::string autominer = bankedTime > 0 ? clockText(bankedTime) : "None";
        const std::string blocks = blockCount(game.state.get("blocks_mined", 0).asDouble());
        const Json& pet = game.state["pet_data"]["active_pet"];
        const std::string petName = pet.isObject() ? pet.get("mood", "Joyful").asString() + " " +
                                                       pet.get("pet_type", "Rabbit").asString()
                                                 : "None";
        const Json& eggs = game.state["pet_data"]["hatchery"];
        int ready = 0;
        for (const auto& egg : eggs)
            if (egg.get("ready", false).asBool())
                ++ready;
        LoreLine hatchery{{"Hatchery: ", foreground},
                          {std::to_string(eggs.size()), {1, 0.8f, 0.2f, 1}},
                          {" hatching", foreground}};
        if (ready > 0) {
            hatchery.push_back({" (", foreground});
            hatchery.push_back({std::to_string(ready), {0.3f, 0.9f, 0.3f, 1}});
            hatchery.push_back({" ready)", foreground});
        }
        const float scoreWidth = std::max({240.f, draw.textWidth("Blocks Mined: " + blocks, 14) + 20,
                                           draw.textWidth("Multi: " + multi, 14) + 20,
                                           draw.textWidth("Autominer: " + autominer, 14) + 20,
                                           draw.textWidth("Pet: " + petName, 14) + 20,
                                           loreWidth(hatchery, 14) + 20});
        Rect score = hudRect("scoreboard", {width - scoreWidth - 15, 55, scoreWidth, 314});
        Color color = dim(background, 0.4f);
        color.a = ctx.settings.get("scoreboard_opacity", 0.75).asFloat();
        draw.rect(score, color);
        float y = score.y + 9;
        auto row = [&](const std::string& caption, const std::string& value, Color tint) {
            draw.text(score.x + 10, y, caption, foreground, 14);
            draw.text(score.x + 10 + draw.textWidth(caption, 14), y, value, tint, 14);
            y += 21;
        };
        row("Money: ", compact(game.state.get("money", 0).asDouble()), {0.3f, 0.9f, 0.3f, 1});
        for (const char* wallet : {"tokens", "sapphires", "rubies"})
            row(game.currencyName(wallet) + ": ", compact(game.state.get(wallet, 0).asDouble()),
                currencyColor(game.data, wallet));
        row("Prestige: ", "P" + number(game.state.get("prestige_level", 0).asDouble()), {1, 0.3f, 0.3f, 1});
        row("Rank: ", game.state.get("rank_name", "A").asString(), {0.2f, 0.8f, 1, 1});
        LoreLine nextRank{{"Next Rankup: ", foreground}};
        const auto value = nextRankText(game);
        nextRank.insert(nextRank.end(), value.begin(), value.end());
        loreLine(score.x + 10, y, nextRank, 14);
        y += 21;
        row("Playtime: ", clockText(game.state.get("playtime_seconds", 0).asDouble()),
            {0.8f, 0.7f, 0.95f, 1});
        row("Blocks Mined: ", blocks, {0.6f, 0.8f, 0.95f, 1});
        row("Pet: ", petName, pet.isObject() ? itemColor(pet) : dim(foreground, 0.6f));
        loreLine(score.x + 10, y, hatchery, 14);
        y += 21;
        row("Points: ", compact(game.state.get("points", 0).asDouble()), {0.3f, 0.9f, 0.3f, 1});
        row("Multi: ", multi, multiplier.remaining > 0 ? foreground : dim(foreground, .6f));
        row("Autominer: ", autominer,
            game.state.get("autominer_active", false).asBool() ? hexColor("#c9705f") : dim(foreground, .6f));
    }
    const Rect timers = bossBars();
    tutorialHud(timers);
    const float size = 46;
    Rect hotbar = hudRect("hotbar", {(width - size * 9) / 2, height - size - 12, size * 9, size});
    int selected = std::clamp(game.state.get("selected_hotbar_index", 0).asInt(), 0, 8);
    for (int i = 0; i < 9; ++i) {
        Rect box{hotbar.x + i * size, hotbar.y, size - 2, size - 2};
        slot(box, "hotbar", i, game.state["inventory_slots"][i], false);
        if (selected == i)
            draw.border(box, foreground, 2);
    }
    const Json& held = game.state["inventory_slots"][selected];
    if (held.isObject()) {
        const auto name = itemTitle(game, held, foreground);
        loreLine((width - loreWidth(name, 15)) / 2, height - 110, name, 15);
    }
    if (held.get("id", "") == "pickaxe") {
        std::string level = "Level: " + std::to_string(game.state.get("pick_level", 1).asInt()) + "  [";
        float start = (width - draw.textWidth(level, 14) - 96) / 2;
        draw.text(start, height - 86, level, foreground, 14);
        start += draw.textWidth(level, 14);
        double progress = game.state.get("pick_xp", 0).asDouble() /
                          std::max(1.0, game.pickXpNeeded(game.state.get("pick_level", 1).asInt()));
        progress = std::clamp(progress, 0.0, 1.0);
        for (int i = 0; i < 12; ++i)
            draw.text(start + i * 7, height - 86, "|",
                      i < int(progress * 12) ? Color{0.3f, 0.95f, 0.3f, 1} : Color{0.85f, 0.35f, 0.35f, 1},
                      14);
        draw.text(start + 84, height - 86, "]", foreground, 14);
    }
    if (ctx.settings.get("show_fps", false).asBool()) {
        std::string text = number(ctx.fps) + " FPS";
        draw.text(width - draw.textWidth(text, 13) - 12, height - 25, text, foreground, 13);
    }
    if (ctx.settings.get("show_inventory_icon", true).asBool()) {
        int occupied = 0;
        for (const auto& item : game.state["inventory_slots"])
            if (item.isObject())
                ++occupied;
        const int capacity = int(game.state["inventory_slots"].size());
        const bool pouch = held.get("id", "") == "pouch";
        Rect counter = hudRect("inventory", {width - 110, height - 104, 98, pouch ? 68.f : 32.f});
        draw.image({counter.x, counter.y, 28, 28},
                   occupied >= capacity ? "textures/inventory_pack_full.png" : "textures/inventory_pack.png");
        label(counter.x + 34, counter.y + 6, std::to_string(occupied) + "/" + std::to_string(capacity), 14);
        if (pouch) {
            occupied = 0;
            for (const auto& item : held["items"])
                if (item.isObject())
                    ++occupied;
            const int pouchCapacity = held.get("slots", 18).asInt();
            draw.item({counter.x, counter.y + 36, 28, 28}, held);
            draw.text(counter.x + 34, counter.y + 42,
                      std::to_string(occupied) + "/" + std::to_string(pouchCapacity),
                      occupied >= pouchCapacity ? Color{.93f, .54f, .48f, 1} : foreground, 14);
        }
    }
    chat();
    if (editing) {
        for (const auto& box : hudBoxes) {
            draw.border(box.second, foreground, 2);
            draw.text(box.second.x + 5, box.second.y + 3, box.first, foreground, 13);
        }
        draw.rect({0, 0, width, 44}, background);
        label(16, 12, "Edit HUD", 17);
        button({width - 216, 6, 100, 31}, "Reset", [this] {
            ctx.settings["hud"] = Json(::Json::objectValue);
            settingsChanged();
        });
        button({width - 108, 6, 96, 31}, "Done", [this] {
            setPage("");
            settingsChanged();
        });
    }
    if (ctx.resetting)
        centered(18, "Refilling mine…", 16);
}

void UI::Impl::chat() {
    if (!ctx.playing || page == "pause" || page == "settings" || page == "bindings" || page == "itemviewer" ||
        page == "theme" || page == "enchant_effects" || page == "title" || page == "worlds" ||
        page == "create" || page == "import")
        return;
    const float width = draw.width(), height = draw.height();
    const bool editing = page == "hud";
    Rect chat = hudRect("chat", {12, height - 210, std::min(510.f, width * 0.48f), 120});
    std::vector<const ChatLine*> recent;
    for (const auto& message : ctx.messages) {
        if (page == "chat" || editing || static_cast<Sint32>(message.expires - SDL_GetTicks()) > 0)
            recent.push_back(&message);
    }
    if (!recent.empty() || editing || page == "chat") {
        Color color = dim(background, 0.4f);
        color.a = ctx.settings.get("chat_opacity", 0.45).asFloat();
        draw.rect(chat, color);
        draw.clip(chat);
        int rows = std::min(6, int(recent.size()));
        for (int i = 0; i < rows; ++i) {
            const auto& message = *recent[recent.size() - rows + i];
            float x = chat.x + 7, y = chat.y + 5 + i * 19;
            if (!message.spans.empty())
                loreLine(x, y, message.spans, 13);
            else if (!message.customColor && message.text.rfind("You: ", 0) == 0) {
                draw.text(x, y, "You: ", {.7f, .85f, 1, 1}, 13);
                draw.text(x + draw.textWidth("You: ", 13), y, message.text.substr(5), foreground, 13);
            } else
                loreLine(
                    x, y,
                    currencyText(game.data, message.text, message.customColor ? message.color : foreground),
                    13);
        }
        draw.unclip();
    }
}

void UI::Impl::tooltip() {
    if (page.empty() || page == "hud" || page == "chat" || leftDown) {
        tooltipTarget.clear();
        tooltipScroll = 0;
        return;
    }
    const Slot* hovered = nullptr;
    for (const auto& value : slots)
        if (value.box.contains(mouseX, mouseY) && value.item.isObject())
            hovered = &value;
    if (!hovered) {
        tooltipTarget.clear();
        tooltipScroll = 0;
        for (const auto& hint : hints)
            if (hint.first.contains(mouseX, mouseY)) {
                float width = std::min(draw.width() - 20, draw.textWidth(hint.second, 14) + 22);
                float x = std::clamp(mouseX + 12, 8.f, draw.width() - width - 8);
                float y = std::min(mouseY + 18, draw.height() - 40);
                draw.rect({x, y, width, 32}, dim(background, .3f));
                draw.border({x, y, width, 32}, outline);
                currencyLabel(x + 10, y + 7, hint.second, 14);
                break;
            }
        return;
    }
    const auto target = hovered->container + ":" + std::to_string(hovered->index);
    if (target != tooltipTarget) {
        tooltipTarget = target;
        tooltipScroll = 0;
    }
    auto lines = itemLoreLines(game, hovered->item, foreground);
    if (lines.empty())
        return;
    if ((hovered->container == "crystals" || hovered->container == "parts") && lines.size() >= 2) {
        lines.resize(2);
        lines.push_back({{"Right-click to unequip.", foreground}});
        lines.push_back({{"Drag back to inventory to unequip.", foreground}});
    }
    float width = 180;
    for (const auto& line : lines)
        width = std::max(width, loreWidth(line, 16) + 24);
    width = std::min(width, draw.width() - 20);
    auto rowHeight = [](const LoreLine& line) {
        return std::any_of(line.begin(), line.end(), [](const TextSpan& span) { return !span.text.empty(); })
                   ? 18.f
                   : 9.f;
    };
    float height = 16;
    for (const auto& line : lines)
        height += rowHeight(line);
    float x = std::min(mouseX + 16, draw.width() - width - 8);
    const float anchor = std::clamp(mouseY + 17, 8.f, std::max(8.f, draw.height() - height - 8));
    const float y = std::clamp(anchor + tooltipScroll, 36.f - height, draw.height() - 36.f);
    tooltipScroll = y - anchor;
    draw.rect({x, y, width, height}, {background.r * 0.3f, background.g * 0.3f, background.b * 0.3f, 0.97f});
    draw.border({x, y, width, height}, outline);
    const float clipTop = std::max(y, 0.f);
    draw.clip({x + 5, clipTop, width - 10, std::min(y + height, draw.height()) - clipTop});
    float lineY = y + 8;
    for (const auto& line : lines) {
        loreLine(x + 11, lineY, line, 16);
        lineY += rowHeight(line);
    }
    draw.unclip();
}

} // namespace prison
