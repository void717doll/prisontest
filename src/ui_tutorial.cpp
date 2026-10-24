#include "ui_internal.hpp"

namespace prison {
std::string UI::Impl::tutorialInstruction() const {
    switch (game.tutorialStep()) {
    case 1:
        return "Mine 10 blocks: " + game.state["tutorial"]["mined"].asString() + "/10";
    case 2:
        return std::string("Open your inventory with ") + SDL_GetKeyName(ctx.settings["inventory_key"].asInt());
    case 3:
        return "Buy an enchant in /enchant";
    case 4:
        return "Earn money and use /rankup";
    default:
        return "Tutorial complete";
    }
}

void UI::Impl::tutorial() {
    Rect box = panel("Getting Started", 590, 360);
    const float x = box.x + 24, width = box.w - 48;
    float y = box.y + 64 - scroll;
    const auto status = game.state["tutorial"].get("status", "skipped").asString();
    const bool active = status == "active";
    const bool fresh = status == "new";
    beginScroll({x, box.y + 58, width, box.h - 137});
    if (active) {
        label(x, y, "Step " + std::to_string(game.tutorialStep()) + " of 4", 17);
        y = paragraph(x, y + 34, width, tutorialInstruction(), 17) + 16;
        const char* instructions[] = {
            "", "Use W A S D to walk, the mouse to look, and Space to jump. Hold left click on a block to mine it.",
            "Your pickaxe stays in the first slot. Inventory has shortcuts to enchants, pets and storage. Esc returns to the mine.",
            "Bright ore gives tokens. Type /enchant in chat, choose Token Enchants, then Efficiency and Buy. Keep mining ore if you need more tokens.",
            "Ordinary blocks earn money automatically. The scoreboard shows the next rank's price. Type /rankup when you can afford it. Ranking up refills the mine."};
        y = paragraph(x, y, width, instructions[game.tutorialStep()], 15);
    } else {
        y = paragraph(x, y, width, status == "completed" ? "The basics are complete." : "Start with your pickaxe.", 19) + 15;
        y = paragraph(x, y, width, "W A S D to move, mouse to look, Space to jump. Hold left click on a block to mine.", 15) + 12;
        y = paragraph(x, y, width,
                      "Mine for money and tokens, buy enchants, then rank up. Four short steps show you how while you play.", 16) + 15;
        y = paragraph(x, y, width, "Skip whenever you like. Progress is saved with this world. Use /tutorial or the pause menu to return.", 15);
    }
    endScroll(y);
    const float half = (width - 12) / 2, bottom = box.y + box.h - 60;
    button({x, bottom, half, 36}, active ? "Continue" : fresh ? "Start Tutorial" : "Restart Tutorial",
           [this, active] { apply(game.tutorialAction(active ? "start" : "restart")); });
    if (active || fresh)
        button({x + half + 12, bottom, half, 36}, "Skip Tutorial",
               [this] { apply(game.tutorialAction("skip")); });
    else
        pageButton({x + half + 12, bottom, half, 36}, "Back to Game", "");
}

void UI::Impl::tutorialHud(Rect timers) {
    if (!page.empty() || game.state["tutorial"].get("status", "") != "active")
        return;
    const float width = std::min(340.f, draw.width() * .43f);
    Rect box{14, 56, width, 96};
    if (timers.x < box.x + box.w && timers.x + timers.w > box.x)
        box.y = std::max(box.y, timers.y + timers.h + 10);
    Color shade = background;
    shade.a = .88f;
    draw.rect(box, shade);
    draw.border(box, outline);
    label(box.x + 12, box.y + 10, "Getting started · " + std::to_string(game.tutorialStep()) + "/4", 14);
    paragraph(box.x + 12, box.y + 33, box.w - 24, tutorialInstruction(), 14);
    label(box.x + 12, box.y + 73,
          game.tutorialStep() == 1 ? "Left click: mine · /tutorial: help or skip" : "/tutorial for help or skip", 12);
}
} // namespace prison
