#include "prison/lore.hpp"
#include <iostream>
#include <stdexcept>

using namespace prison;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool same(Color a, Color b) {
    return std::abs(a.r - b.r) < .001 && std::abs(a.g - b.g) < .001 && std::abs(a.b - b.b) < .001;
}
std::string plain(const LoreLine& line) {
    std::string result;
    for (const auto& span : line)
        result += span.text;
    return result;
}
LoreLine find(const std::vector<LoreLine>& lines, const std::string& prefix) {
    for (const auto& line : lines)
        if (plain(line).rfind(prefix, 0) == 0)
            return line;
    throw std::runtime_error("missing lore: " + prefix);
}
} // namespace

int main(int argc, char** argv) {
    try {
        Data data;
        data.load(argc > 1 ? argv[1] : "..");
        Game game(data);
        game.newWorld(true);
        const Color white{1, 1, 1, 1};
        const auto gradient = namedText("&#005155&lc&#075657&lh&r!", white);
        check(plain(gradient) == "ch!" && gradient.size() == 3 && gradient[0].style.bold &&
                  gradient[1].style.bold && !gradient[2].style.bold &&
                  same(gradient[0].color, hexColor("#005155")) &&
                  same(gradient[1].color, hexColor("#075657")) && same(gradient[2].color, white),
              "minecraft gradients must retain per-letter colours and reset formatting");
        const auto legacyName = namedText("&a&l&o&n&mTool&f Plain", white);
        check(plain(legacyName) == "Tool Plain" && legacyName[0].style.bold && legacyName[0].style.italic &&
                  legacyName[0].style.underline && legacyName[0].style.strikethrough &&
                  !legacyName[1].style.bold,
              "minecraft style codes must apply and reset on a new colour");
        const auto bungeeName = namedText("&x&1&2&3&4&5&6&lHex", white);
        check(plain(bungeeName) == "Hex" && same(bungeeName[0].color, hexColor("#123456")) &&
                  bungeeName[0].style.bold,
              "bungee hex gradients must render as one colour code");
        check(plainName("§a§lGreen§r & unknown &#bad") == "Green & unknown &#bad",
              "section-sign codes and literal invalid markup must stay consistent");
        prison::Json earnings;
        earnings["money"] = 1850000;
        earnings["tokens"] = 159910000;
        const auto summary = miningSummaryText(data, earnings);
        check(plain(summary) == "[1m] +1.85M | +159.91M" && same(summary[0].color, white) &&
                  same(summary[1].color, hexColor("#aaaaaa")) && same(summary[2].color, white) &&
                  same(summary[3].color, {.3f, .9f, .3f, 1}) && same(summary[4].color, white) &&
                  same(summary[5].color, currencyColor(data, "tokens")),
              "summary brackets, timer, amounts and separator have independent colors");
        auto rank = nextRankText(game);
        check(rank.size() == 4 && same(rank[1].color, white) && same(rank[3].color, white) &&
                  same(rank[2].color, hexColor("#ff5555")),
              "unaffordable rank price stays red inside white brackets");
        game.state["money"] = game.rankCost(1);
        check(same(nextRankText(game)[2].color, hexColor("#55ff55")),
              "exactly enough money makes the rank price green");
        const auto greed = currencyText(data, "Token Greed: 1075.", white);
        for (const auto& span : greed)
            if (span.text.find_first_not_of(' ') != std::string::npos)
                check(same(span.color, currencyColor(data, "tokens")),
                      "token greed label and value must share a color");
        prison::Json item;
        item["id"] = "crystal";
        item["tier"] = 2;
        auto title = itemTitle(game, item, white);
        check(title.size() == 2 && title[0].text == "T2 " && same(title[0].color, hexColor("#969696")) &&
                  same(title[1].color, white),
              "tier color must be brightened without tinting the whole title");
        check(!same(tierColor(12), tierColor(13)) && !same(tierColor(13), tierColor(14)),
              "higher tiers must keep distinct source colors");
        item["id"] = "pet_egg";
        item["tier_id"] = "common";
        title = itemTitle(game, item, white);
        check(title[0].text == "Common" && same(title[0].color, hexColor("#44c767")) &&
                  title[1].text == " Egg" && same(title[1].color, white),
              "egg rarity color leaks into suffix");
        item["id"] = "pet";
        item["tier_id"] = "overpowered";
        item["pet_color"] = "#123456";
        check(same(itemTitle(game, item, white)[0].color, hexColor("#123456")), "pet name ignores pet color");
        const auto rarity = find(itemLoreLines(game, item, white), "Rarity: ");
        check(rarity.size() > 3 && !same(rarity[1].color, rarity[2].color), "pet rarity gradient lost");

        item = prison::Json();
        item["id"] = "pickaxe";
        game.state["parts_prestige"] = 3;
        game.state["pick_custom_name"] = "<&#ff0000;>Red &#00ff00Green";
        title = itemTitle(game, item, white);
        check(plain(title) == "Red Green (P4)", "rename color codes displayed as text");
        check(same(title[0].color, hexColor("#ff0000")) && same(title[1].color, hexColor("#00ff00")) &&
                  same(title[3].color, tierColor(4)),
              "rename and prestige need independent colors");
        game.state["pick_custom_name"] = "[color=red]literal";
        check(plain(itemTitle(game, item, white)) == "[color=red]literal (P4)",
              "metadata interpreted as markup");
        game.state["pick_xp"] = game.pickXpNeeded(1) / 2;
        game.state["enchants"]["efficiency"] = 3;
        auto lines = itemLoreLines(game, item, white);
        auto meter = find(lines, "Level: ");
        check(meter[1].text == "||||||" && meter[2].text == "||||||" &&
                  same(meter[1].color, hexColor("#55ff55")) && same(meter[2].color, hexColor("#ff5555")),
              "pickaxe progress meter lost its green and red segments");
        auto enchant = find(lines, "Efficiency:");
        check(enchant[0].color.r > enchant[3].color.r && same(enchant[1].color, hexColor("#555555")),
              "enchant label, separator and value need separate colors");
        game.state["enchant_label_color"] = "#102030";
        game.state["enchant_value_color"] = "#304050";
        enchant = find(itemLoreLines(game, item, white), "Efficiency:");
        check(same(enchant[0].color, hexColor("#102030")) && same(enchant[3].color, hexColor("#304050")),
              "custom enchant colors ignored");
        game.state["enchant_colors_enabled"] = false;
        enchant = find(itemLoreLines(game, item, white), "Efficiency:");
        check(!same(enchant[0].color, hexColor("#102030")), "enchantcolor off retains custom color");

        for (const auto& row : std::vector<std::pair<std::string, std::string>>{
                 {"liquid_xp", "#5796a1"}, {"automine_time", "#c9705f"}, {"multiplier", "#8fd6ff"}}) {
            item = prison::Json();
            item["id"] = row.first;
            const auto lore = itemLoreLines(game, item, white);
            check(same(lore[0][0].color, hexColor(row.second)) && lore[1].size() == 2 &&
                      same(lore[1][1].color, hexColor(row.second)),
                  "item title and value colors do not match");
        }
        std::cout << "lore color checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
