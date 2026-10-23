#include "prison/ui.hpp"
#include "prison/world.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace prison;
using Value = prison::Json;

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Sdl {
    Sdl() {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
            throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }
    ~Sdl() {
        SDL_Quit();
    }
};

struct WindowDeleter {
    void operator()(SDL_Window* window) const {
        SDL_DestroyWindow(window);
    }
};

std::unique_ptr<SDL_Window, WindowDeleter> window() {
    auto value = std::unique_ptr<SDL_Window, WindowDeleter>(
        SDL_CreateWindow("prisoncpp ui regression", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280,
                         800, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
    if (!value)
        throw std::runtime_error(SDL_GetError());
    return value;
}

struct Point {
    float x, y;
};

Rect panel(float width, float height) {
    return {(1280 - width) / 2, (800 - height) / 2, width, height};
}

Point inventorySlot(int index, float height) {
    Rect box = panel(690, height);
    int row = index < 9 ? 3 : (index - 9) / 9;
    int column = index % 9;
    float x = box.x + (box.w - 43 * 9) / 2 + column * 43;
    float y = box.y + box.h - 4 * 43 - 32 + row * 43 + (row == 3 ? 5 : 0);
    return {x + 20, y + 20};
}

Value item(const char* id) {
    Value result;
    result["id"] = id;
    result["count"] = 1;
    return result;
}

struct Fixture {
    Game game;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    Renderer renderer;
    UiContext context;
    UI ui;
    std::filesystem::path screenshots;

    Fixture(const Data& data, std::filesystem::path pictures)
        : game(data), window_(window()), renderer(window_.get(), data), ui(game, renderer, context),
          screenshots(std::move(pictures)) {
        SDL_GL_SetSwapInterval(0);
        context.settings = defaultSettings();
        context.playing = true;
        context.buildHash = "ui-test";
        context.worlds = Value(::Json::arrayValue);
        fresh();
        if (!screenshots.empty())
            std::filesystem::create_directories(screenshots);
    }

    void fresh() {
        game.newWorld(true);
        game.random.seed(12);
        for (unsigned i = 1; i < game.state["inventory_slots"].size(); ++i)
            game.state["inventory_slots"][i] = Value();
        context.actions.clear();
        context.messages.clear();
        context.playing = true;
        ui.open("");
        paint();
    }

    void paint(const std::string& image = "") {
        renderer.begin(1);
        renderer.beginUI();
        ui.draw();
        if (!image.empty() && !screenshots.empty())
            renderer.screenshot(screenshots / (image + ".png"));
        renderer.present();
    }

    void open(const std::string& page) {
        ui.open(page);
        paint();
    }

    std::vector<unsigned char> pixels() {
        renderer.begin(1);
        renderer.beginUI();
        ui.draw();
        return readPixels();
    }

    std::vector<unsigned char> readPixels() {
        std::vector<unsigned char> image(1280 * 800 * 4);
        renderer.flush();
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, 1280, 800, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
        return image;
    }

    void wheel(int steps) {
        SDL_Event event{};
        event.type = SDL_MOUSEWHEEL;
        event.wheel.y = steps;
        ui.event(event);
    }

    void motion(Point point) {
        SDL_Event event{};
        event.type = SDL_MOUSEMOTION;
        event.motion.x = int(std::lround(point.x));
        event.motion.y = int(std::lround(point.y));
        ui.event(event);
    }

    void mouse(Point point, bool down, Uint8 button = SDL_BUTTON_LEFT) {
        SDL_Event event{};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button = button;
        event.button.x = int(std::lround(point.x));
        event.button.y = int(std::lround(point.y));
        ui.event(event);
    }

    void click(Point point, Uint8 button = SDL_BUTTON_LEFT) {
        motion(point);
        mouse(point, true, button);
        mouse(point, false, button);
        paint();
    }

    void drag(Point from, Point to) {
        motion(from);
        mouse(from, true);
        motion(to);
        paint();
        mouse(to, false);
        paint();
    }

    void key(SDL_Keycode key, Uint16 modifiers = KMOD_NONE) {
        SDL_Event event{};
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = key;
        event.key.keysym.mod = modifiers;
        ui.event(event);
        paint();
    }

    void text(const char* value) {
        SDL_Event event{};
        event.type = SDL_TEXTINPUT;
        std::strncpy(event.text.text, value, sizeof(event.text.text) - 1);
        ui.event(event);
        paint();
    }

    bool action(const std::string& kind, const std::string& value = "") const {
        for (const auto& row : context.actions)
            if (row.kind == kind && (value.empty() || row.value == value))
                return true;
        return false;
    }
};

void enchants(Fixture& f) {
    f.fresh();
    f.game.state["tokens"] = 1000000000.0;
    f.open("enchants");
    check(f.ui.capturesInput(), "enchant menu must own input");
    Rect grid = panel(690, 540);
    f.click({grid.x + 60, grid.y + 168});
    check(f.ui.menu() == "enchant_detail", "enchant card opens its detail");
    Rect detail = panel(565, 515);
    const float x = detail.x + 24, width = detail.w - 48;
    const float buttonWidth = (width - 6 * 7) / 7;
    f.click({x + 4 * (buttonWidth + 7) + buttonWidth / 2, detail.y + 293});
    double before = f.game.state["tokens"].asDouble();
    double expected = f.game.enchantCost("efficiency", 11);
    f.click({640, detail.y + 398});
    check(f.game.state["enchants"]["efficiency"].asInt() == 11, "plus-ten then buy purchases eleven levels");
    check(f.game.state["tokens"].asDouble() == before - expected, "purchase charges the displayed amount");

    f.game.state["tokens"] = f.game.enchantCost("efficiency", 7);
    f.paint();
    f.click({x + 6 * (buttonWidth + 7) + buttonWidth / 2, detail.y + 293});
    f.click({640, detail.y + 398});
    check(f.game.state["enchants"]["efficiency"].asInt() == 18, "max selects only affordable levels");
    check(f.game.state["tokens"].asDouble() == 0, "max purchase cannot overdraw tokens");
    f.paint("enchant-detail");

    f.open("enchants");
    f.click({grid.x + 60, grid.y + 168});
    check(f.ui.menu() == "enchant_detail", "unaffordable enchant can be inspected");
    f.click({640, detail.y + 398});
    check(f.game.state["tokens"].asDouble() == 0, "unaffordable purchase stays blocked");
    f.open("enchants");
    f.game.state["tokens"] = 1000000000.0;
    f.paint();
    const float tabWidth = (grid.w - 56) / 3;
    f.click({grid.x + 20 + 2 * (tabWidth + 8) + tabWidth / 2, grid.y + 74});
    f.click({grid.x + 60, grid.y + 168});
    check(f.ui.menu() == "enchant_detail", "locked ruby enchant can be inspected");
    auto beforeLocked = f.game.snapshot();
    f.click({640, detail.y + 398});
    check(f.game.snapshot() == beforeLocked, "locked enchant purchase cannot mutate the world");
    f.open("enchants");
    f.paint("enchant-grid");
}

void inventory(Fixture& f) {
    f.fresh();
    Value crystal = item("crystal");
    crystal["tier"] = 1;
    f.game.state["inventory_slots"][1] = crystal;
    f.game.state["pick_level"] = 20;
    f.open("inventory");
    f.click(inventorySlot(1, 400), SDL_BUTTON_RIGHT);
    const Value identified = f.game.state["inventory_slots"][1];
    check(identified.get("identified", false).asBool(), "right-click identifies crystal immediately");
    check(identified.get("count", 0).asInt() == 1, "identifying preserves stack count");

    f.game.state["cleaner_settings"]["enabled"] = true;
    f.game.state["cleaner_settings"]["default_action"] = "salvage";
    f.game.state["cleaner_settings"]["rules"] = Value(::Json::arrayValue);
    f.open("crystals");
    Rect crystals = panel(690, 590);
    Point equipped{640 - 24 + 22, crystals.y + 96 + 22};
    f.drag(inventorySlot(1, 590), equipped);
    check(f.game.state["inventory_slots"][1].isNull(), "drag removes original inventory item immediately");
    check(f.game.state["equipped_crystals"][0] == identified, "drag preserves equipped metadata");
    f.click(equipped, SDL_BUTTON_RIGHT);
    check(f.game.state["equipped_crystals"][0].isNull(), "right-click removes equipment immediately");
    check(f.game.state["inventory_slots"][1] == identified, "manual return bypasses automatic filter");
    f.paint("crystals");

    Value part = item("pick_part");
    part["identified"] = true;
    part["tier"] = 1;
    part["slot"] = "head";
    part["boost_type"] = "sell";
    part["boost"] = .034;
    part["uid"] = "ui-test-head";
    f.game.state["inventory_slots"][2] = part;
    f.open("parts");
    f.click(inventorySlot(2, 590), SDL_BUTTON_RIGHT);
    check(f.game.state["equipped_parts"]["head"] == part, "part right-click equips correct slot and tier");
    check(f.game.state["inventory_slots"][2].isNull(), "equipped part removed from original slot");
    f.paint("parts");

    Value pet = item("pet");
    pet["uid"] = "ui-test-pet";
    pet["tier_id"] = "common";
    pet["mood"] = "Happy";
    pet["pet_type"] = "Sheep";
    pet["level"] = 1;
    pet["exp"] = 0;
    pet["boost_type"] = "exp";
    pet["base_boost_pct"] = 5;
    f.game.state["pet_data"]["active_pet"] = pet;
    f.open("pets");
    Rect pets = panel(690, 590);
    f.click({pets.x + 48, pets.y + 114}, SDL_BUTTON_RIGHT);
    check(f.game.state["pet_data"]["active_pet"].isNull(), "pet right-click clears equipped slot");
    check(f.game.state["inventory_slots"][2] == pet, "pet returns without disappearing under filter");
    f.paint("pets");

    f.open("inventory");
    f.motion({490, 757});
    f.paint("inventory-over-hotbar");
    f.key(SDLK_ESCAPE);
    check(f.ui.menu().empty() && !f.ui.capturesInput(), "escape closes equipment menu to gameplay");
}

void storage(Fixture& f) {
    const char* sizes[] = {"small", "medium", "large", "vault"};
    const int counts[] = {18, 27, 54, prison::VaultPageSize};
    for (int i = 0; i < 4; ++i) {
        f.fresh();
        bool vault = i == 3;
        if (!vault)
            check(f.game.command(std::string("/givepouch ") + sizes[i]).ok, "create pouch fixture");
        Value crystal = item("crystal");
        crystal["tier"] = 4;
        crystal["identified"] = true;
        crystal["boost_type"] = "token";
        crystal["boost"] = .12;
        crystal["uid"] = "stored-crystal";
        f.game.state["inventory_slots"][10] = crystal;
        f.game.state["cleaner_settings"]["enabled"] = true;
        f.game.state["cleaner_settings"]["default_action"] = "salvage";
        f.open(vault ? "vault" : "pouch:1");
        float controls = vault ? 82 : 0;
        float height = ((counts[i] + 8) / 9) * 43 + 299 + controls;
        Rect box = panel(435, height);
        Point lastSlot{box.x + 24 + 8 * 43 + 20, box.y + 57 + controls + (counts[i] / 9 - 1) * 43 + 20};
        f.drag(inventorySlot(10, height), lastSlot);
        Value stored = vault ? f.game.state["vault_slots"] : f.game.state["inventory_slots"][1]["items"];
        check(stored[counts[i] - 1] == crystal, "last storage slot accepts drag and preserves metadata");
        check(f.game.state["inventory_slots"][10].isNull(), "storage drag removes the inventory copy");
        f.motion({15, 400});
        f.paint(std::string("storage-") + sizes[i]);
        f.drag(lastSlot, inventorySlot(10, height));
        stored = vault ? f.game.state["vault_slots"] : f.game.state["inventory_slots"][1]["items"];
        check(stored[counts[i] - 1].isNull(), "storage withdrawal clears its old slot");
        check(f.game.state["inventory_slots"][10] == crystal, "manual storage return bypasses filter");
        f.key(SDLK_ESCAPE);
        check(f.ui.menu().empty(), "escape closes storage directly to gameplay");
    }
}

void settings(Fixture& f) {
    f.fresh();
    f.open("settings");
    Rect box = panel(690, 555);
    const float x = box.x + 22, y = box.y + 62, half = (box.w - 56) / 2;
    f.click({x + half + 12 + half / 2, y + 369});
    check(f.game.state["message_settings"]["visual_enchants"] == true && f.ui.menu() == "settings",
          "one settings toggle enables all mine enchant visuals without a submenu");
    f.paint("enchant-visuals");
    f.click({x + half + 12 + half / 2, y + 369});
    check(f.game.state["message_settings"]["visual_enchants"] == false, "enchant visuals can be disabled");
    check(f.context.settings["vsync"].asBool(), "vsync is enabled by default");
    f.click({x + half + 12 + half / 2, y + 69});
    check(!f.context.settings["vsync"].asBool(), "first settings page exposes vsync toggle");
    check(f.action("settings"), "vsync toggle requests persistence");
    f.click({x + half / 2, y + 269});
    check(f.ui.menu() == "itemviewer", "settings item viewer button opens dedicated page");
    f.click({x + (box.w - 44) * .8f, y + 69});
    check(f.context.settings["held_item_scale"].asDouble() > 1.5, "held item scale slider updates setting");
    check(f.action("settings"), "slider change requests persistence");
    f.click({box.x + box.w - 49, box.y + 26});
    check(f.ui.menu() == "settings", "item viewer done returns to settings");
    f.key(SDLK_ESCAPE);
    check(!f.ui.capturesInput(), "settings escape closes directly to gameplay");

    f.key(SDLK_RSHIFT);
    check(f.ui.menu() == "hud" && f.ui.capturesInput(), "right shift opens hud editor and gates gameplay");
    const Value hudBefore = f.context.settings["hud"];
    f.drag({640, 400}, {690, 440});
    check(f.context.settings["hud"] == hudBefore, "crosshair cannot be moved");
    f.drag({600, 760}, {560, 705});
    check(f.context.settings["hud"]["hotbar"].isObject(), "hotbar can be dragged");
    f.key(SDLK_RSHIFT);
    check(f.ui.menu().empty(), "right shift closes hud editor");
    f.key(SDLK_F8);
    check(f.ui.menu() == "hud", "documented F8 shortcut opens hud editor");
    f.key(SDLK_F8);
    check(f.ui.menu().empty(), "F8 also closes hud editor");

    f.open("theme");
    const Value original = f.context.settings["background"];
    f.click({x + 190, y + 18});
    f.key(SDLK_a, KMOD_CTRL);
    f.text("\\000000");
    f.click({640, y + 243});
    check(f.context.settings["background"] == original, "invalid pasted color leaves theme unchanged");
    check(!f.context.messages.empty(), "invalid color provides feedback without crashing");
    f.click({x + 190, y + 18});
    f.key(SDLK_a, KMOD_CTRL);
    f.text("#102030");
    f.click({640, y + 243});
    check(f.context.settings["background"] == "#102030", "valid pasted theme color applies");
    f.paint("theme");
}

void chatAndWorlds(Fixture& f) {
    f.fresh();
    f.key(SDLK_SLASH);
    check(f.ui.menu() == "chat" && f.ui.capturesInput(), "slash opens chat and gates movement");
    f.text("/");
    f.text("pets");
    f.key(SDLK_RETURN);
    check(f.action("command", "/pets"), "chat opener cannot duplicate slash in submitted command");
    check(f.ui.menu().empty(), "submitting chat restores gameplay input");
    f.ui.apply({false, "test error", ""});
    check(f.context.messages.back().customColor && f.context.messages.back().color.r == 1 &&
              std::abs(f.context.messages.back().color.g - .6f) < .001f &&
              std::abs(f.context.messages.back().color.b - .5f) < .001f,
          "command errors preserve source salmon color");
    f.ui.apply({true, "test success", ""});
    check(f.context.messages.back().customColor &&
              std::abs(f.context.messages.back().color.r - .55f) < .001f &&
              f.context.messages.back().color.g == 1,
          "command success preserves source green color");
    f.context.say("ordinary message");
    check(!f.context.messages.back().customColor, "ordinary chat follows theme foreground");
    f.ui.apply(f.game.command("/clear"));
    check(f.context.messages.empty(), "clear command empties chat without opening a menu");

    f.context.playing = false;
    f.open("title");
    check(f.ui.capturesInput(), "title owns input");
    f.click({640, 355});
    check(f.ui.menu() == "worlds", "singleplayer title button opens world list");
    Rect worlds = panel(610, 480);
    f.click({worlds.x + 130, worlds.y + worlds.h - 44});
    check(f.ui.menu() == "create", "world list create button opens world form");
    f.click({640, worlds.y + 192});
    check(f.context.settings["new_devmode"].asBool(), "new world form devmode checkbox is interactive");
    f.click({640, worlds.y + worlds.h - 48});
    check(f.action("new", "Your Mine"), "create form queues named world creation");
    f.paint("create-world");

    f.open("worlds");
    f.click({worlds.x + worlds.w - 130, worlds.y + worlds.h - 44});
    check(f.ui.menu() == "import", "world list exposes import form");
    Rect importer = panel(610, 320);
    f.click({640, importer.y + 110});
    f.text("/tmp/world with spaces.json");
    f.key(SDLK_RETURN);
    check(f.action("import", "/tmp/world with spaces.json"), "import preserves the complete file path");
    f.paint("import-world");
}

void helpMenu(Fixture& f) {
    f.fresh();
    f.ui.apply(f.game.command("/help crystals"));
    f.paint("help-search");
    check(f.ui.menu() == "help" && f.context.messages.empty(), "help opens without chat spam");
    Rect box = panel(730, 570);
    f.click({box.x + 80, box.y + 73});
    auto commands = f.pixels();
    f.click({box.x + 205, box.y + 73});
    check(f.pixels() != commands, "guide articles have their own tab separate from commands");
    f.paint("help-article");
    f.click({box.x + box.w - 50, box.y + 117});
    f.motion({15, 400});
    f.paint("help-topics");
    f.click({box.x + 80, box.y + 73});
    auto category = f.pixels();
    f.click({box.x + 120, box.y + 192});
    check(f.pixels() != category, "selecting ranks and prestige changes the command list");
    f.click({box.x + 220, box.y + 117});
    f.key(SDLK_a, KMOD_CTRL);
    f.text("unmatched_help_text");
    check(f.game.state["_help_search"] == "unmatched_help_text", "help search updates as the user types");
    f.paint("help-empty");
    f.click({box.x + box.w - 50, box.y + 117});
    check(f.game.state["_help_search"] == "", "clear removes the help search");
    f.text("/endmulti");
    f.paint("help-commands");
    check(f.game.state["_help_search"] == "/endmulti", "clear leaves the help search focused");
    f.click({box.x + box.w - 50, box.y + 117});
    f.click({box.x + 205, box.y + 73});
    f.motion({box.x + 130, box.y + 200});
    auto before = f.pixels();
    f.wheel(-4);
    check(f.pixels() != before, "help results scroll independently from the article");
    f.paint("help-guide-scrolled");
    f.key(SDLK_ESCAPE);
    check(f.ui.menu().empty(), "help escape returns directly to gameplay");
}

void feedbackAndMultipliers(Fixture& f) {
    f.fresh();
    f.open("automine");
    Rect autominer = panel(560, 380);
    f.click({640, autominer.y + 247});
    check(f.ui.menu() == "automine" && f.context.messages.back().text == "You need Automine Time.",
          "failed menu actions report to chat and keep the menu open");
    f.paint("autominer-error-chat");
    f.context.messages.clear();
    auto before = f.pixels();
    for (bool success : {false, true}) {
        f.ui.apply({success, "feedback belongs in chat"});
        check(f.context.messages.size() == 1, "menu feedback is delivered once");
        f.context.messages.clear();
        check(f.pixels() == before, "success and failure cannot leave an inline status message");
    }
    f.game.state["autominer_time"] = 3600;
    f.paint();
    f.click({640, autominer.y + 247});
    check(f.ui.menu().empty() && f.action("mine_entry"), "autominer start closes to the mine entrance");
    f.open("automine");
    f.click({640, autominer.y + 247});
    check(f.ui.menu().empty() && !f.game.state["autominer_active"].asBool(), "autominer stop closes to game");

    f.fresh();
    check(f.game.command("/givemultiplier 2 1h 4").ok, "create combinable sell multiplier stack");
    f.open("combinemulti");
    f.paint("combine-multipliers");
    Rect box = panel(690, 420);
    f.click({box.x + 490, box.y + 137});
    check(f.ui.menu() == "combinemulti" && f.game.state["inventory_slots"][1]["amount"].asDouble() == 4 &&
              f.game.state["inventory_slots"][1]["duration"].asDouble() == 10800,
          "recipe button combines and refreshes in place");
    auto combined = f.game.state["inventory_slots"];
    f.paint();
    f.click({box.x + 490, box.y + 137});
    check(f.game.state["inventory_slots"] == combined, "incomplete recipe button cannot consume items");
    f.ui.apply(f.game.useItem(1));
    f.open("");
    f.paint("multiplier-countdown");
    f.game.state["parts_prestige"] = 5;
    f.game.state["autominer_time"] = 3600;
    f.paint("multiplier-with-equipment");

    f.context.playing = false;
    f.open("import");
    f.context.messages.clear();
    f.ui.apply({false, "Unable to import this world."});
    f.paint("title-error-chat");
    check(f.context.messages.size() == 1, "title errors also use chat");
}

void progression(Fixture& f) {
    f.fresh();
    f.game.state["money"] = 1e25;
    f.open("more");
    Rect more = panel(600, 570);
    f.click({more.x + 140, more.y + more.h - 105});
    check(f.action("progression", "rankup"), "rank button queues progression behind earned rewards");
    check(f.game.state["rank_index"].asInt() == 0, "ui cannot spend money before pending rewards settle");
    f.game.state["rank_index"] = 25;
    f.game.state["rank_name"] = "Z";
    f.ui.apply(f.game.prestige(false));
    f.paint();
    check(f.ui.menu() == "confirm_prestige", "prestige quote opens its confirmation");
    Rect confirm = panel(530, 330);
    f.click({confirm.x + 140, confirm.y + confirm.h - 66});
    check(f.action("progression", "prestige_confirm"), "prestige confirmation queues ordered transaction");
    check(f.game.state["prestige_level"].asInt() == 0,
          "confirmation waits for coordinator before changing progression");
    check(!f.ui.capturesInput(), "prestige confirmation returns directly to gameplay");
}

void devtools(Fixture& f) {
    f.fresh();
    f.open("test");
    Rect box = panel(730, 590);
    float x = box.x + 24, width = box.w - 48, half = (width - 16) / 2;
    float itemY = box.y + 107;
    auto edit = [&](Point point, const char* text) {
        f.click(point);
        f.key(SDLK_a, KMOD_CTRL);
        f.text(text);
    };
    edit({x + 100, itemY + 173}, "3");
    edit({x + half + 116, itemY + 173}, "4");
    f.click({x + 100, itemY + 230});
    f.click({x + width - 15, itemY + 230});
    f.click({x + 100, itemY + 287});
    edit({x + half + 116, itemY + 287}, "12");
    const Point give{640, box.y + box.h - 54};
    f.motion(give);
    f.mouse(give, true);
    for (int i = 0; i < 5; ++i) {
        f.paint();
        f.mouse(give, true);
    }
    f.mouse(give, false);
    f.paint("dev-items");
    int total = 0;
    for (const auto& item : f.game.state["inventory_slots"]) {
        if (item.get("id", "") != "crystal")
            continue;
        total += item.get("count", 1).asInt();
        check(item.get("tier", 0).asInt() == 4, "dev item form applies selected tier");
        check(item.get("identified", false).asBool(), "dev item form applies identified option");
        check(item.get("boost_type", "") == "token", "dev item form applies selected boost type");
        check(std::abs(item.get("boost", 0).asDouble() - .12) < 1e-8,
              "dev item form applies explicit percentage");
    }
    check(total == 3, "give item runs once while mouse is held despite duplicate down events");
    check(!f.game.state["_stress"].get("active", false).asBool(), "item grant cannot start a stress job");

    float tabWidth = (width - 32) / 5;
    f.click({x + tabWidth + 8 + tabWidth / 2, box.y + 75});
    float pickY = box.y + 111;
    edit({x + 100, pickY + 35}, "Test Tool");
    edit({x + 100, pickY + 94}, "100");
    edit({x + half + 116, pickY + 94}, "4");
    f.click({640, pickY + 137});
    check(f.game.state["pick_custom_name"] == "Test Tool" && f.game.state["pick_level"].asInt() == 100 &&
              f.game.state["parts_prestige"].asInt() == 3,
          "pickaxe form applies independent name level and prestige");
    const auto beforeEnchant = f.game.state["enchants"];
    f.paint();
    check(f.game.state["enchants"] == beforeEnchant, "opening the editor cannot grant max enchants");
    f.click({640, pickY + 255});
    check(f.game.state["enchants"]["efficiency"] == f.game.data.enchants["defs"]["efficiency"]["max"],
          "enchant editor defaults to the selected enchant maximum");
    edit({x + half + 116, pickY + 213}, "17");
    f.click({640, pickY + 255});
    check(f.game.state["enchants"].get("efficiency", 0).asInt() == 17,
          "enchant editor applies selected level");
    f.paint("dev-pickaxe");

    f.click({x + 2 * (tabWidth + 8) + tabWidth / 2, box.y + 75});
    f.context.actions.clear();
    Point rank{x + half / 2, box.y + 236};
    f.mouse(rank, true);
    f.mouse(rank, true);
    f.mouse(rank, false);
    f.paint("dev-progression");
    check(f.context.actions.size() == 1 && f.action("progression", "rankup"),
          "ordinary dev rankup queues one progression action");
    check(!f.game.state["_stress"].get("active", false).asBool(), "ordinary rankup never starts stress");
    f.context.actions.clear();
    f.click({x + half / 2, box.y + 286});
    check(f.context.actions.size() == 1 && f.action("progression", "prestige"),
          "ordinary prestige queues one confirmation request");
    f.context.actions.clear();
    f.click({x + half + 16 + half / 2, box.y + 286});
    check(f.context.actions.size() == 1 && f.action("command", "/resetmine"),
          "resetmine is a single ordinary reset");

    f.click({x + 4 * (tabWidth + 8) + tabWidth / 2, box.y + 75});
    for (int i = 0; i < 3; ++i)
        f.click({x + width - 15, box.y + 150});
    f.context.actions.clear();
    f.click({x + half / 2, box.y + 394});
    check(f.context.actions.size() == 1 && f.action("command", "/test stress prestige"),
          "only explicit stress start queues repeated progression");
    f.ui.apply(f.game.command("/test stress prestige"));
    f.open("test");
    f.game.state["_stress"]["iterations"] = 15;
    f.paint("dev-stress");
    f.context.actions.clear();
    f.click({x + half + 16 + half / 2, box.y + 394});
    check(f.context.actions.size() == 1 && f.action("command", "/test stress stop"),
          "stress page stop button queues explicit cancellation");
    f.ui.apply(f.game.command("/test stress stop"));
    check(!f.game.state["_stress"].get("active", true).asBool(), "stop command ends running stress job");
}

void tutorial(Fixture& f) {
    f.fresh();
    f.game.newWorld();
    f.open("");
    check(f.ui.menu() == "tutorial" && f.ui.capturesInput(), "new world opens the welcome menu");
    f.paint("tutorial-welcome");
    const auto box = panel(590, 360);
    f.click({box.x + box.w - 145, box.y + box.h - 42});
    check(!f.ui.capturesInput() && f.game.state["tutorial"]["status"] == "skipped" && f.action("save"),
          "skip closes onboarding and requests a save");
    f.ui.apply(f.game.command("/tutorial"));
    f.paint();
    f.click({box.x + 145, box.y + box.h - 42});
    check(!f.ui.capturesInput() && f.game.state["tutorial"]["status"] == "active",
          "restart begins the tutorial without blocking mining");
    f.paint("tutorial-mining");
    for (int i = 0; i < 10; ++i)
        f.game.mineBlock("tier_a_smooth", false);
    f.key(SDLK_e);
    check(f.game.tutorialStep() == 3, "inventory key completes the inventory lesson");
    f.ui.apply(f.game.command("/tutorial skip"));
    f.game.newWorld();
    f.open("");
    f.key(SDLK_ESCAPE);
    check(!f.ui.capturesInput() && f.game.state["tutorial"]["status"] == "skipped",
          "escape dismisses the initial onboarding without reopening it");
    f.fresh();
}

void miningHover(Fixture& f) {
    f.fresh();
    f.context.settings = defaultSettings();
    f.motion({15, 400});
    auto baseline = f.pixels();
    f.motion({456, 760});
    auto hover = f.pixels();
    check(baseline == hover,
          "hovering gameplay hotbar cannot highlight slots or display item lore while mining");
    f.paint("mining-hover");
}

void itemTooltips(Fixture& f) {
    f.fresh();
    check(f.game.command("/test max").ok, "tooltip fixture gets max pickaxe");
    f.open("inventory");
    f.motion(inventorySlot(0, 400));
    f.paint("max-pick-tooltip");
    const auto unscrolled = f.pixels();
    f.wheel(3);
    check(f.pixels() != unscrolled, "wheel must move hovered tooltip even when it fits the screen");
    f.paint("scrolled-pick-tooltip");
    f.wheel(-3);
    check(f.pixels() == unscrolled, "opposite wheel motion restores tooltip position");
    f.wheel(1000);
    const auto atTop = f.pixels();
    f.wheel(1000);
    check(f.pixels() == atTop, "tooltip scrolling stops before every row disappears");
    f.motion({15, 400});
    f.pixels();
    f.motion(inventorySlot(0, 400));
    check(f.pixels() == unscrolled, "leaving and returning resets tooltip scroll");
    check(f.game.command("/enchantcolor #20ccff #ffcc40").ok, "tooltip fixture sets enchant colors");
    f.paint("custom-pick-tooltip");
    Value crystal = item("crystal");
    crystal["tier"] = 4;
    crystal["identified"] = true;
    crystal["boost_type"] = "token";
    crystal["boost"] = .25;
    crystal["uid"] = "tooltip-crystal";
    f.game.state["inventory_slots"][1] = crystal;
    f.paint();
    f.motion(inventorySlot(1, 400));
    f.paint("crystal-tooltip");
    Value pet = item("pet");
    pet["tier_id"] = "legendary";
    pet["mood"] = "Happy";
    pet["pet_type"] = "Fox";
    pet["pet_color"] = "#ffeea3";
    pet["level"] = 5;
    pet["exp"] = 42;
    pet["boost_type"] = "sell";
    pet["base_boost_pct"] = 75;
    pet["uid"] = "tooltip-pet";
    f.game.state["inventory_slots"][2] = pet;
    f.paint();
    f.motion(inventorySlot(2, 400));
    f.paint("pet-tooltip");
}

void savedHud(Fixture& f) {
    f.fresh();
    f.context.settings["hud"]["hotbar"]["x"] = "invalid";
    f.context.settings["hud"]["hotbar"]["y"] = Value(::Json::objectValue);
    f.context.settings["hud"]["scoreboard"]["x"] = 1e30;
    f.game.state["pick_xp"] = 1e30;
    f.paint("saved-hud");
    check(!f.ui.capturesInput(), "invalid saved hud positions cannot block gameplay");
}

void discoveryViews(Fixture& f) {
    f.fresh();

    Data data = f.game.data;
    data.systems["discoveries"]["pocket_chance"] = 1;
    Mine mine(data);
    mine.reset(6, 0, 72);
    while (mine.resetting() || mine.pendingChunks())
        mine.step();
    const auto saved = mine.snapshot();
    auto cellAt = [&](int at) {
        return Cell{mine.minimum().x + at % mine.width(),
                    mine.minimum().y + at / (mine.width() * mine.depth()),
                    mine.minimum().z + at / mine.width() % mine.depth()};
    };
    std::vector<Cell> rare;
    for (const auto& row : saved["pocket_cells"])
        rare.push_back(cellAt(row.asInt()));
    const auto first = rare.front();
    for (int y = first.y; y <= mine.maximum().y; ++y)
        for (int x = first.x - 4; x <= first.x + 4; ++x)
            for (int z = first.z - 4; z <= first.z + 4; ++z) {
                bool pocket = false;
                for (const auto& cell : rare)
                    pocket |= cell.x == x && cell.y == y && cell.z == z;
                if (!pocket)
                    mine.dig({x, y, z});
            }
    while (mine.pendingChunks())
        mine.step();
    f.renderer.begin(1, mine.skyColor());
    f.renderer.world(mine, {float(first.x + 3), float(first.y + 5), float(first.z + 5)}, .54f, -.65f, 72);
    if (!f.screenshots.empty())
        f.renderer.screenshot(f.screenshots / "rare-ore-pocket.png");
    mine.reset(16, 0, 72);
    while (mine.resetting() || mine.pendingChunks())
        mine.step();
    f.renderer.begin(1, mine.skyColor());
    f.renderer.world(mine, mine.spawn() + Vec3{0, 1.62f, 0}, 0, -.25f, 72);
    if (!f.screenshots.empty())
        f.renderer.screenshot(f.screenshots / "rank-theme.png");
    f.open("guardian");
    f.paint("guardian-points");
    f.open("mines");
    f.paint("rebirth-mines");
    f.open("more");
    f.paint("more-menu");
    f.fresh();
}

void rendererChecks(Fixture& f) {
    Mine mine(f.game.data);
    mine.reset(0, 0, 1234);
    while (mine.resetting() || mine.pendingChunks())
        mine.step();
    for (int y = mine.minimum().y; y <= mine.maximum().y; ++y)
        for (int z = mine.minimum().z; z <= mine.maximum().z; ++z)
            for (int x = mine.minimum().x; x <= mine.maximum().x; ++x)
                if ((x + y + z) % 4 == 0)
                    mine.dig({x, y, z});
    while (mine.pendingChunks())
        mine.step();
    auto compareTerrain = [&](const Mine& source, Vec3 eye, float yaw, float pitch, float fov) {
        f.renderer.begin(1);
        f.renderer.world(source, eye, yaw, pitch, fov);
        auto culled = f.readPixels();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // draw every face with the same camera as an independent visibility reference.
        glBegin(GL_TRIANGLES);
        auto draw = [](const ChunkMesh& mesh) {
            for (const auto& vertex : mesh.vertices) {
                glColor3f(vertex.r, vertex.g, vertex.b);
                glTexCoord2f(vertex.u, vertex.v);
                glVertex3f(vertex.x, vertex.y, vertex.z);
            }
        };
        draw(source.shellMesh());
        for (const auto& chunk : source.chunks())
            draw(chunk);
        glEnd();
        check(culled == f.readPixels(), "terrain culling removed a visible face");
    };
    compareTerrain(mine, {0, 3, 18}, 0, -.3f, 72);
    compareTerrain(mine, {0, 3, 18}, 3.14159265f, 0, 72);
    check(f.renderer.stats().culledChunks > 0 && f.renderer.stats().terrainDraws == 1,
          "looking away must only submit the surrounding shell");
    compareTerrain(mine, {0, -3, 0}, 0, -.3f, 72);
    compareTerrain(mine, {12, -.5f, 12}, -1.57f, -.6f, 50);
    compareTerrain(mine, {-12, 1, 0}, 1.57f, -.4f, 120);
    compareTerrain(mine, {0, 25, 0}, 0, -1.48f, 72);
    compareTerrain(mine, {0, -3, 0}, 1.2f, .8f, 72);
    compareTerrain(mine, {0, 1, -13}, 3.14159265f, -.1f, 72);
    mine.dig({0, mine.maximum().y, 0});
    while (mine.pendingChunks())
        mine.step();
    compareTerrain(mine, {0, 3, 18}, 0, -.3f, 72);
    check(f.renderer.stats().terrainUploads > 0, "visible mining edits must update their gpu mesh");
    compareTerrain(mine, {0, 3, 18}, 0, -.3f, 72);
    check(f.renderer.stats().terrainUploads == 0, "unchanged terrain must reuse its gpu buffers");
    mine.reset(1, 0, 4321);
    while (mine.resetting() || mine.pendingChunks())
        mine.step();
    compareTerrain(mine, {0, 3, 18}, 0, -.3f, 72);
    Mine other(f.game.data);
    other.reset(16, 0, 72);
    while (other.resetting() || other.pendingChunks())
        other.step();
    compareTerrain(other, {0, 3, 18}, 0, -.3f, 72);
    check(f.renderer.stats().terrainUploads > 0, "a different mine must replace cached terrain");
    Mine replacement(f.game.data);
    replacement.reset(6, 0, 4321);
    while (replacement.resetting() || replacement.pendingChunks())
        replacement.step();
    compareTerrain(replacement, {0, 3, 18}, 0, -.3f, 72);
    compareTerrain(mine, {0, 3, 18}, 0, -.3f, 72);

    auto settings = defaultSettings();
    auto pickaxe = item("pickaxe");
    auto held = [&](const Value& value) {
        f.renderer.begin(1);
        f.renderer.held(value, 3, settings, 0);
        return f.readPixels();
    };
    auto first = held(pickaxe);
    check(f.renderer.stats().heldUploads == 1, "held geometry must upload on first use");
    check(first == held(pickaxe) && f.renderer.stats().heldUploads == 0,
          "cached held geometry must preserve the image without another upload");
    settings["tool_angle"] = 30;
    check(first != held(pickaxe) && f.renderer.stats().heldUploads == 0,
          "changing hand view must rotate the cached model");
    if (!f.screenshots.empty())
        f.renderer.screenshot(f.screenshots / "cached-pickaxe.png");
    auto crystal = item("crystal");
    crystal["tier"] = 4;
    held(crystal);
    check(f.renderer.stats().heldUploads == 1, "switching held items must replace cached geometry");

    auto pet = item("pet");
    pet["pet_type"] = "sheep";
    pet["tier_id"] = "common";
    for (const auto& value : {pickaxe, pet, Value()})
        for (float angle : {30.f, 90.f})
            for (float swing : {0.f, .35f}) {
                settings["tool_angle"] = angle;
                settings["left_handed"] = false;
                f.renderer.begin(1);
                f.renderer.held(value, 3, settings, swing);
                const auto right = f.readPixels();
                settings["left_handed"] = true;
                f.renderer.begin(1);
                f.renderer.held(value, 3, settings, swing);
                const auto left = f.readPixels();
                std::size_t differences = 0;
                // mirrored interpolation can round a color channel one value differently.
                for (int y = 0; y < 800; ++y)
                    for (int x = 0; x < 1280; ++x)
                        for (int channel = 0; channel < 3; ++channel)
                            if (std::abs(int(left[(y * 1280 + x) * 4 + channel]) -
                                         int(right[(y * 1280 + 1279 - x) * 4 + channel])) > 1)
                                ++differences;
                check(differences < 80, "left hand must mirror the full right-hand image and swing");
            }
    settings["tool_angle"] = 90;
    held(pickaxe);
    if (!f.screenshots.empty())
        f.renderer.screenshot(f.screenshots / "left-hand-pickaxe.png");
    settings["left_handed"] = false;
    const auto plainHeld = held(pickaxe);
    f.renderer.begin(1);
    const auto clear = f.readPixels();
    f.renderer.beginUI();
    f.renderer.mineHologram(mine, {-3.8f, 4.9f, 18}, 0, 0, 72, 1);
    const auto label = f.readPixels();
    f.renderer.held(pickaxe, 3, settings, 0);
    const auto overLabel = f.readPixels();
    int overlap = 0;
    for (std::size_t i = 0; i < plainHeld.size(); i += 4)
        if (std::memcmp(plainHeld.data() + i, clear.data() + i, 3) != 0 &&
            std::memcmp(label.data() + i, clear.data() + i, 3) != 0) {
            ++overlap;
            check(std::memcmp(plainHeld.data() + i, overLabel.data() + i, 3) == 0,
                  "held item must cover previously drawn world labels");
        }
    check(overlap > 10, "hologram overlap fixture must intersect held geometry");
    if (!f.screenshots.empty())
        f.renderer.screenshot(f.screenshots / "held-over-label.png");

    auto fonts = [&](bool flushEach) {
        f.renderer.begin(1);
        f.renderer.beginUI();
        for (int size = 9; size <= 24; ++size) {
            f.renderer.text(10, (size - 9) * 30, "glyph cache: 123 café", {1, .7f, .3f, 1}, size);
            if (flushEach)
                f.renderer.flush();
        }
        return f.readPixels();
    };
    check(fonts(false) == fonts(true),
          "font cache eviction must flush pending text before deleting an atlas");

    auto clippedText = [&](bool flushEach) {
        f.renderer.begin(1);
        f.renderer.beginUI();
        for (int i = 0; i < 100; ++i) {
            if (i % 3)
                f.renderer.clip({20, 30, 270, 160});
            else
                f.renderer.unclip();
            float y = 20.f + (i % 12) * 16;
            f.renderer.rect({10, y, 360, 18}, {.1f, .2f, .4f, .4f});
            f.renderer.text(10, y, "clipped text: 0123456789 abcdefghijklmnopqrstuvwxyz", {1, .8f, .5f, 1},
                            13);
            if (flushEach)
                f.renderer.flush();
        }
        f.renderer.unclip();
        return f.readPixels();
    };
    check(clippedText(false) == clippedText(true),
          "overflowing a ui batch must preserve clipping and translucent draw order");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Data data;
        data.load(argc > 1 ? argv[1] : PRISON_RESOURCE_DIR);
        Sdl sdl;
        Fixture fixture(data, argc > 2 ? argv[2] : "");
        enchants(fixture);
        inventory(fixture);
        storage(fixture);
        settings(fixture);
        tutorial(fixture);
        chatAndWorlds(fixture);
        helpMenu(fixture);
        feedbackAndMultipliers(fixture);
        progression(fixture);
        devtools(fixture);
        itemTooltips(fixture);
        miningHover(fixture);
        savedHud(fixture);
        rendererChecks(fixture);
        discoveryViews(fixture);
        std::cout << "ui regression checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "ui regression failure: " << error.what() << '\n';
        return 1;
    }
}
