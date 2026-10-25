#include "ui_internal.hpp"
#include <cstring>

namespace prison {
void UI::Impl::eraseLast(std::string& text) {
    if (text.empty())
        return;
    std::size_t index = text.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80)
        --index;
    text.resize(index);
}

void UI::Impl::updateSlider() {
    if (sliderKey.empty())
        return;
    double fraction = std::clamp(double(mouseX - sliderBox.x) / sliderBox.w, 0.0, 1.0);
    double value = sliderMin + fraction * (sliderMax - sliderMin);
    ctx.settings[sliderKey] = std::round(value * 100) / 100;
}

void UI::Impl::rightClick(const Slot& value) {
    if (value.container == "preview")
        return;
    if (value.container == "inventory") {
        if (page == "pet_food" || (page == "bin" && value.item["id"] == "pick_part")) {
            const bool parts = page == "bin";
            auto& selection = parts ? partSelection : petFoodSelection;
            if (value.index > 0 && value.item["id"] == (parts ? "pick_part" : "pet")) {
                if (selection.count(value.index))
                    selection.erase(value.index);
                else
                    selection[value.index] = value.item;
            }
        } else if (page == "bin" && value.index > 0 && value.item.isObject()) {
            Json args;
            args["index"] = value.index;
            args["item"] = value.item;
            apply(game.itemAction("salvage", args));
        } else if (page == "combinemulti" && value.index > 0 && value.item.isObject()) {
            confirmAction = "combinemulti";
            confirmArgs = Json();
            confirmArgs["index"] = value.index;
            confirmArgs["item"] = value.item;
            setPage("confirm");
        } else
            apply(game.useItem(value.index));
    } else if (value.container != "hotbar")
        apply(game.transfer(value.container, value.index, "inventory"));
}

void UI::Impl::transferTo(const Slot& target) {
    if (selectedIndex < 0)
        return;
    if (target.container == selectedContainer && target.index == selectedIndex)
        return;
    Result result;
    if (selectedContainer == "inventory" && (target.container == "pet_food" || target.container == "pet") &&
        game.state["inventory_slots"][selectedIndex].get("id", "") == "pet_food") {
        Json args;
        args["index"] = selectedIndex;
        result = game.itemAction("feed", args);
    } else
        result = game.transfer(selectedContainer, selectedIndex, target.container, target.index);
    selectedContainer.clear();
    selectedIndex = -1;
    apply(result);
}

void UI::Impl::event(const SDL_Event& event) {
    if (event.type == SDL_MOUSEMOTION) {
        mouseX = event.motion.x / draw.scale();
        mouseY = event.motion.y / draw.scale();
        if (leftDown && !sliderKey.empty())
            updateSlider();
        if (leftDown && !movingHud.empty()) {
            ctx.settings["hud"][movingHud]["x"] = std::clamp((mouseX - hudOffsetX) / draw.width(), 0.f, 1.f);
            ctx.settings["hud"][movingHud]["y"] = std::clamp((mouseY - hudOffsetY) / draw.height(), 0.f, 1.f);
        }
        return;
    }
    if (event.type == SDL_MOUSEWHEEL && !page.empty()) {
        if (page == "help" && helpListBox.contains(mouseX, mouseY)) {
            helpListScroll = std::clamp(helpListScroll - event.wheel.y * 38.f, 0.f, helpListMaximum);
            return;
        }
        bool onItem = false;
        for (const auto& value : slots)
            if (value.box.contains(mouseX, mouseY) && value.item.isObject()) {
                onItem = true;
                const auto target = value.container + ":" + std::to_string(value.index);
                if (tooltipTarget != target) {
                    tooltipTarget = target;
                    tooltipScroll = 0;
                }
            }
        if (onItem)
            tooltipScroll -= event.wheel.y * 36.f;
        else
            scroll = std::clamp(scroll - event.wheel.y * 38.f, 0.f, scrollMaximum);
        return;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN && !page.empty()) {
        mouseX = event.button.x / draw.scale();
        mouseY = event.button.y / draw.scale();
        if (event.button.button == SDL_BUTTON_RIGHT) {
            for (auto it = slots.rbegin(); it != slots.rend(); ++it)
                if (it->box.contains(mouseX, mouseY)) {
                    Slot value = *it;
                    rightClick(value);
                    return;
                }
        }
        if (event.button.button != SDL_BUTTON_LEFT)
            return;
        if (leftDown)
            return;
        leftDown = true;
        downX = mouseX;
        downY = mouseY;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
            if (it->box.contains(mouseX, mouseY)) {
                auto callback = it->press;
                callback();
                return;
            }
        if (page == "hud")
            for (const auto& element : hudBoxes)
                if (element.second.contains(mouseX, mouseY)) {
                    movingHud = element.first;
                    hudOffsetX = mouseX - element.second.x;
                    hudOffsetY = mouseY - element.second.y;
                    return;
                }
        for (auto it = slots.rbegin(); it != slots.rend(); ++it)
            if (it->box.contains(mouseX, mouseY)) {
                Slot value = *it;
                if (value.container == "preview")
                    return;
                if (selectedIndex >= 0 &&
                    (selectedContainer != value.container || selectedIndex != value.index)) {
                    transferTo(value);
                    return;
                }
                if (page == "bin" || page == "pet_food" || page == "combinemulti") {
                    rightClick(value);
                    return;
                }
                if (value.item.isObject()) {
                    selectedContainer = value.container;
                    selectedIndex = value.index;
                }
                return;
            }
        field.clear();
        SDL_StopTextInput();
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        mouseX = event.button.x / draw.scale();
        mouseY = event.button.y / draw.scale();
        if (leftDown && selectedIndex >= 0 && std::abs(mouseX - downX) + std::abs(mouseY - downY) > 5)
            for (const auto& value : slots)
                if (value.box.contains(mouseX, mouseY)) {
                    transferTo(value);
                    break;
                }
        leftDown = false;
        if (!sliderKey.empty() || !movingHud.empty())
            settingsChanged();
        sliderKey.clear();
        movingHud.clear();
    }
    if (event.type == SDL_TEXTINPUT && !field.empty()) {
        if (field == "chat" && chatOpeningKey && SDL_GetTicks() - chatOpened < 100 &&
            event.text.text[0] == chatOpeningKey && !event.text.text[1]) {
            chatOpeningKey = 0;
            return;
        }
        chatOpeningKey = 0;
        if (inputs[field].size() < 1024)
            inputs[field] += event.text.text;
        if (field == "help_search")
            searchHelp();
        return;
    }
    if (event.type != SDL_KEYDOWN || event.key.repeat)
        return;
    SDL_Keycode key = event.key.keysym.sym;
    if (!listening.empty()) {
        if (key != SDLK_ESCAPE) {
            ctx.settings[listening] = int(key);
            settingsChanged();
        }
        listening.clear();
        return;
    }
    if (key == SDLK_ESCAPE) {
        if (page == "statistics")
            setPage(statisticsReturn);
        else if (page == "pet_food")
            setPage("pets");
        else if (page == "confirm" && confirmAction == "pet_food")
            setPage(confirmArgs.get("pets", Json()).isArray() ? "pet_food" : "pets");
        else if (page == "confirm" && confirmAction == "salvage_parts")
            setPage("bin");
        else
            setPage(ctx.playing ? (page.empty() ? "pause" : "") : "title");
        return;
    }
    if (!field.empty()) {
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && field == "help_search") {
            searchHelp();
            return;
        }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && field == "import_path") {
            if (!inputs[field].empty())
                action("import", inputs[field]);
            return;
        }
        if (key == SDLK_BACKSPACE)
            eraseLast(inputs[field]);
        if ((event.key.keysym.mod & KMOD_CTRL) && key == SDLK_v) {
            char* text = SDL_GetClipboardText();
            if (text) {
                if (inputs[field].size() + std::strlen(text) <= 1024)
                    inputs[field] += text;
                SDL_free(text);
            }
        }
        if ((event.key.keysym.mod & KMOD_CTRL) && key == SDLK_a)
            inputs[field].clear();
        if (field == "help_search")
            searchHelp();
        if (page == "chat") {
            if (key == SDLK_UP && !history.empty()) {
                historyIndex = std::max(0, historyIndex - 1);
                inputs[field] = history[historyIndex];
            }
            if (key == SDLK_DOWN) {
                historyIndex = std::min(int(history.size()), historyIndex + 1);
                inputs[field] = historyIndex == int(history.size()) ? "" : history[historyIndex];
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                const std::string text = inputs[field];
                if (!text.empty()) {
                    history.push_back(text);
                    if (history.size() > 100)
                        history.erase(history.begin());
                    if (text[0] == '/')
                        command(text);
                    else
                        ctx.say("You: " + text);
                }
                setPage("");
            }
        }
        return;
    }
    if ((key == SDLK_RSHIFT || key == SDLK_F8) && ctx.playing) {
        setPage(page == "hud" ? "" : "hud");
        return;
    }
    if (ctx.playing && (key == ctx.settings["inventory_key"].asInt() || key == SDLK_TAB ||
                        key == ctx.settings["pickaxe_key"].asInt())) {
        setPage(page.empty() ? (key == ctx.settings["inventory_key"].asInt() ? "inventory" : "pick") : "");
        return;
    }
    if (ctx.playing && page.empty() && (key == SDLK_t || key == SDLK_SLASH)) {
        setPage("chat");
        chatOpeningKey = key == SDLK_SLASH ? '/' : 't';
        chatOpened = SDL_GetTicks();
        if (key == SDLK_SLASH)
            inputs["chat"] = "/";
    }
}

} // namespace prison
