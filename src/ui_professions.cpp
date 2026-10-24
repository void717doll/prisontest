#include "ui_internal.hpp"

namespace prison {
void UI::Impl::professions() {
    Rect box = panel("Professions", 590, 370);
    float x = box.x + 24, y = box.y + 65, width = box.w - 48;
    y = paragraph(x, y, width,
                  "Each profession has its own wallet, tool, enchants, parts and storage. "
                  "Switching keeps your progress.") + 15;
    for (const auto* name : {"mining", "fishing", "farming"}) {
        const bool active = game.data.profession == name;
        const std::string title = std::string(name) == "mining" ? "Mining"
                                   : std::string(name) == "fishing" ? "Fishing" : "Farming";
        Json tool;
        tool["id"] = "pickaxe";
        tool["profession"] = name;
        draw.item({x, y, 36, 36}, tool);
        button({x + 48, y, width - 48, 36}, title + (active ? " · Current" : ""),
               [this, name] { action("profession", name); }, !active);
        y += 53;
    }
}

void UI::Impl::activityIntro() {
    const bool fishing = game.data.profession == "fishing";
    Rect box = panel(fishing ? "Fishing" : "Farming", 620, 410);
    float x = box.x + 24, y = box.y + 65, width = box.w - 48;
    y = paragraph(x, y, width,
                  fishing ? "Take your rod to the pond. Aim at the water and right-click to cast. "
                            "When the bobber dips and the bar says Bite, right-click again to reel in."
                          : "Take your hoe to an empty plot and right-click to plant. Use the watering "
                            "can to double its growth speed, then harvest the ripe crop with your hoe.") + 13;
    y = paragraph(x, y, width,
                  fishing ? "Your first bait is free. Sell catches and buy more bait at the supplies table "
                            "or /supplies. Fish vary in size; rarer fish unlock as your rod levels up."
                          : "Start with nine plots and wheat seeds. Select crops and buy seeds at /supplies. "
                            "Crops keep growing while you play another profession, and never wither.") + 13;
    paragraph(x, y, width, "Use /tool to enchant and equip parts or crystals. "
                          "Only this profession's currencies pay for its upgrades.");
    const float half = (width - 12) / 2;
    button({x, box.y + box.h - 60, half, 36}, "Show Guide", [this] { apply(game.activityAction("intro")); });
    button({x + half + 12, box.y + box.h - 60, half, 36}, "Skip",
           [this] { apply(game.activityAction("skip")); });
}

void UI::Impl::supplies() {
    if (game.data.profession == "mining") {
        professions();
        return;
    }
    const bool fishing = game.data.profession == "fishing";
    const auto& config = game.data.systems["professions"][game.data.profession];
    const auto& activity = game.state["activity"];
    Rect box = panel(fishing ? "Fishing Supplies" : "Farming Supplies", 660, 540);
    float x = box.x + 24, width = box.w - 48;
    currencyLabel(x, box.y + 60, compact(game.state["money"].asDouble()) + " " + game.currencyName("money"));
    Rect list{x, box.y + 94, width, box.h - 165};
    beginScroll(list);
    float y = list.y - scroll;
    if (fishing) {
        label(x, y + 7, "Bait: " + std::to_string(activity["bait"].asInt()), 16);
        button({x + width - 280, y, 280, 34},
               "Buy " + config["bait_batch"].asString() + " bait · " +
                   compact(config["bait_price"].asDouble()) + " coins",
               [this] { apply(game.activityAction("bait")); });
        y += 53;
        for (const auto& fish : config["fish"]) {
            Json icon;
            icon["id"] = "fish";
            icon["species"] = fish["id"];
            icon["profession"] = "fishing";
            draw.item({x, y, 32, 32}, icon);
            label(x + 44, y + 6,
                  fish["label"].asString() + " · rod level " + fish["level"].asString() + " · " +
                      fish["min_cm"].asString() + "–" + fish["max_cm"].asString() + " cm", 14);
            y += 40;
        }
    } else {
        button({x, y, width, 32}, "Get Watering Can", [this] { apply(game.activityAction("watering_can")); });
        y += 48;
        for (const auto& crop : config["crops"]) {
            const auto id = crop["id"].asString();
            const bool unlocked = game.state["pick_level"].asInt() >= crop["level"].asInt();
            Json icon;
            icon["id"] = "crop";
            icon["species"] = id;
            icon["profession"] = "farming";
            draw.item({x, y, 32, 32}, icon);
            label(x + 42, y + 5,
                  crop["label"].asString() + " · " + activity["seeds"][id].asString() +
                      " seeds · level " + crop["level"].asString(), 14);
            button({x, y + 37, (width - 12) / 2, 32}, activity["selected_crop"] == id ? "Selected" : "Select",
                   [this, id] { apply(game.activityAction("select_seed", id)); }, unlocked);
            button({x + (width + 12) / 2, y + 37, (width - 12) / 2, 32},
                   "9 seeds · " + compact(crop["seed_price"].asDouble() * 9) + " coins",
                   [this, id] { apply(game.activityAction("seeds", id)); }, unlocked);
            y += 82;
        }
        const int expansion = activity["plots_owned"].asInt() / 9 - 1;
        const std::string title = expansion >= 2 ? "All 27 plots unlocked"
            : "9 more plots · level " + config["plot_levels"][expansion].asString() + " · " +
                  compact(config["plot_costs"][expansion].asDouble()) + " coins";
        button({x, y, width, 32}, title, [this] { apply(game.activityAction("plots")); }, expansion < 2);
        y += 42;
    }
    button({x, y, width, 32}, "Out of supplies and coins? Get a starter supply",
           [this] { apply(game.activityAction("starter")); });
    y += 42;
    endScroll(y);
    button({x, box.y + box.h - 55, width, 34}, fishing ? "Sell Catches" : "Sell Crops",
           [this] { apply(game.activityAction("sell")); });
}

void UI::Impl::activityHud() {
    if (!page.empty() || game.data.profession == "mining")
        return;
    auto& activity = game.state["activity"];
    if (activity.get("guide", false) == true) {
        const bool fishing = game.data.profession == "fishing";
        const int step = activity["intro_step"].asInt();
        std::string instruction;
        if (step >= 3)
            instruction = "Sell your " + std::string(fishing ? "catches" : "crops") +
                          " at /supplies. Upgrade with /tool.";
        else if (fishing)
            instruction = game.state["_cast"].isObject() ? "Wait for a bite, then right-click to reel in."
                                                       : "Rod: aim at the pond and right-click to cast.";
        else if (step == 0)
            instruction = "Hoe: right-click an empty plot to plant wheat.";
        else if (step == 1)
            instruction = "Get your watering can at /supplies, then water the plot.";
        else
            instruction = "Harvest the ripe crop with your hoe.";
        const Rect box{14, 56, std::min(340.f, draw.width() * .43f), 128};
        Color shade = background;
        shade.a = .88f;
        draw.rect(box, shade);
        draw.border(box, outline);
        label(box.x + 12, box.y + 10, fishing ? "Fishing · getting started" : "Farming · getting started", 14);
        paragraph(box.x + 12, box.y + 33, box.w - 24, instruction, 14);
        label(box.x + 12, box.y + 104, "/tutorial skip to hide this guide", 12);
    }
    if (!ctx.activityHint.empty())
        centered(draw.height() - 139, ctx.activityHint, 14);
}
} // namespace prison
