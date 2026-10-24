#include "prison/lore.hpp"
#include <cctype>

namespace prison {
namespace {
const Json& rarity(const Game& game, const Json& item) {
    const auto& tiers = game.data.systems["pets"]["tiers"];
    for (const auto& tier : tiers)
        if (tier["id"] == item.get("tier_id", "common"))
            return tier;
    return tiers[0];
}
LoreLine splitValue(const std::string& text, const std::string& prefix, Color foreground, Color value) {
    return {{prefix, foreground}, {text.substr(prefix.size()), value}};
}
} // namespace

LoreLine namedText(const std::string& value, Color foreground) {
    static const char* palette[] = {"#000000", "#0000aa", "#00aa00", "#00aaaa", "#aa0000", "#aa00aa",
                                    "#ffaa00", "#aaaaaa", "#555555", "#5555ff", "#55ff55", "#55ffff",
                                    "#ff5555", "#ff55ff", "#ffff55", "#ffffff"};
    LoreLine result;
    std::string segment;
    Color color = foreground;
    TextStyle style;
    auto markerSize = [&value](std::size_t at) -> std::size_t {
        if (at < value.size() && value[at] == '&')
            return 1;
        return value.compare(at, 2, "§") == 0 ? 2 : 0;
    };
    for (std::size_t i = 0; i < value.size();) {
        std::size_t end = i;
        Color nextColor = color;
        TextStyle nextStyle = style;
        const bool wrapped = value.compare(i, 3, "<&#") == 0;
        const auto marker = markerSize(i);
        const auto prefix = wrapped ? 3 : marker + 1;
        if ((wrapped || (marker && i + marker < value.size() && value[i + marker] == '#')) &&
            i + prefix + 6 <= value.size()) {
            const std::string hex = value.substr(i + prefix, 6);
            auto after = i + prefix + 6;
            bool valid = hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
            if (wrapped) {
                if (value.compare(after, 2, ";>") == 0)
                    after += 2;
                else if (value.compare(after, 1, ">") == 0)
                    ++after;
                else
                    valid = false;
            }
            if (valid) {
                nextColor = hexColor(hex);
                nextStyle = {};
                end = after;
            }
        } else if (marker && i + marker < value.size()) {
            const char code = char(std::tolower(static_cast<unsigned char>(value[i + marker])));
            const auto tint = std::string("0123456789abcdef").find(code);
            if (tint != std::string::npos) {
                nextColor = hexColor(palette[tint]);
                nextStyle = {};
                end = i + marker + 1;
            } else if (code == 'x') {
                auto after = i + marker + 1;
                std::string hex;
                for (int digit = 0; digit < 6; ++digit) {
                    const auto length = markerSize(after);
                    if (!length || after + length >= value.size() ||
                        !std::isxdigit(static_cast<unsigned char>(value[after + length])))
                        break;
                    hex += value[after + length];
                    after += length + 1;
                }
                if (hex.size() == 6) {
                    nextColor = hexColor(hex);
                    nextStyle = {};
                    end = after;
                }
            } else if (code == 'l' || code == 'o' || code == 'n' || code == 'm' || code == 'r') {
                if (code == 'l')
                    nextStyle.bold = true;
                else if (code == 'o')
                    nextStyle.italic = true;
                else if (code == 'n')
                    nextStyle.underline = true;
                else if (code == 'm')
                    nextStyle.strikethrough = true;
                else {
                    nextColor = foreground;
                    nextStyle = {};
                }
                end = i + marker + 1;
            }
        }
        if (end > i) {
            if (!segment.empty())
                result.push_back({std::move(segment), color, style});
            segment.clear();
            color = nextColor;
            style = nextStyle;
            i = end;
            continue;
        }
        segment += value[i++];
    }
    if (!segment.empty())
        result.push_back({std::move(segment), color, style});
    return result;
}
std::string plainName(const std::string& value) {
    std::string result;
    for (const auto& span : namedText(value, {}))
        result += span.text;
    return result;
}

LoreLine currencyText(const Data& data, const std::string& text, Color foreground) {
    struct Word {
        std::size_t start, end;
        std::string key, currency;
    };
    std::vector<Word> words;
    for (std::size_t start = text.find_first_not_of(" \t\r\n"); start != std::string::npos;) {
        auto end = text.find_first_of(" \t\r\n", start);
        if (end == std::string::npos)
            end = text.size();
        std::string key = text.substr(start, end - start);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        while (!key.empty() && (key.back() == ':' || key.back() == '.' || key.back() == ','))
            key.pop_back();
        words.push_back({start, end, key, ""});
        start = text.find_first_not_of(" \t\r\n", end);
    }
    auto amount = [](const std::string& value) {
        const auto start = value.find_first_not_of("+-");
        return start != std::string::npos && std::isdigit(static_cast<unsigned char>(value[start]));
    };
    for (std::size_t i = 0; i < words.size(); ++i) {
        auto& word = words[i];
        std::string currency;
        if (word.key == "token" || word.key == "tokens") {
            if (i > 0 && words[i - 1].key == "rename")
                continue;
            currency = "tokens";
        } else if (word.key == "ruby" || word.key == "rubys" || word.key == "rubies")
            currency = "rubies";
        else if (word.key == "sapphire" || word.key == "sapphires")
            currency = "sapphires";
        else
            continue;
        word.currency = currency;
        if (i + 1 < words.size() && words[i + 1].key == "greed") {
            words[i + 1].currency = currency;
            if (i + 2 < words.size() && amount(words[i + 2].key))
                words[i + 2].currency = currency;
        }
        if (i > 0 && amount(words[i - 1].key))
            words[i - 1].currency = currency;
        if (i + 1 < words.size() && amount(words[i + 1].key))
            words[i + 1].currency = currency;
    }
    LoreLine line;
    std::size_t offset = 0;
    for (const auto& word : words) {
        if (word.currency.empty())
            continue;
        if (word.start > offset)
            line.push_back({text.substr(offset, word.start - offset), foreground});
        line.push_back({text.substr(word.start, word.end - word.start), currencyColor(data, word.currency)});
        offset = word.end;
    }
    if (offset < text.size())
        line.push_back({text.substr(offset), foreground});
    return line;
}

LoreLine miningSummaryText(const Data& data, const Json& summary) {
    const Color white{1, 1, 1, 1}, money{.3f, .9f, .3f, 1};
    LoreLine line{
        {"[", white},   {"1m", hexColor("#aaaaaa")},
        {"] ", white},  {"+" + compact(summary.get("money", 0).asDouble()), money},
        {" | ", white}, {"+" + compact(summary.get("tokens", 0).asDouble()), currencyColor(data, "tokens")}};
    for (const char* wallet : {"rubies", "sapphires"})
        if (summary.get(wallet, 0).asDouble() > 0) {
            line.push_back({" | ", white});
            line.push_back({"+" + compact(summary[wallet].asDouble()), currencyColor(data, wallet)});
        }
    if (summary.get("points", 0).asDouble() > 0) {
        line.push_back({" | ", white});
        line.push_back({"+" + compact(summary["points"].asDouble()) + " points", hexColor("#55ff55")});
    }
    return line;
}

LoreLine nextRankText(const Game& game) {
    const int rank = game.state.get("rank_index", 0).asInt();
    const Color rankColor{.2f, .8f, 1, 1}, white{1, 1, 1, 1};
    if (rank >= 25)
        return {{"Prestige", rankColor}};
    const double cost = game.rankCost(rank + 1);
    return {{std::string(1, char('B' + rank)), rankColor},
            {" [", white},
            {compact(cost), hexColor(game.state.get("money", 0).asDouble() >= cost ? "#55ff55" : "#ff5555")},
            {"]", white}};
}

Color itemNameColor(const Game& game, const Json& item, Color fallback) {
    const auto id = item.get("id", "").asString();
    if (id == "pet" || id == "pet_egg") {
        Color color = hexColor(rarity(game, item).get("color", "#44c767").asString());
        return id == "pet" ? hexColor(item.get("pet_color", "").asString(), color) : color;
    }
    if (id == "pickaxe")
        return tierColor(game.state.get("parts_prestige", 0).asInt() + 1);
    if (id == "robot") {
        const auto kind = item.get("boost_type", "money").asString();
        return kind == "token" ? currencyColor(game.data, "tokens")
                               : hexColor(kind == "xp" ? "#66ddff" : "#f0c66a");
    }
    if (id == "crystal" || id == "crystal_dust" || id == "pick_part")
        return tierColor(item.get("tier", 1).asInt());
    if (id == "pouch") {
        const auto size = item.get("tier", "small").asString();
        return tierColor(size == "large" ? 3 : size == "medium" ? 2 : 1);
    }
    const std::pair<const char*, const char*> colors[] = {
        {"rename_token", "#98775d"}, {"enchant_book", "#8d57bb"}, {"minebomb", "#ff5555"},
        {"pet_food", "#8b5a2b"},     {"liquid_xp", "#5796a1"},    {"automine_time", "#c9705f"},
        {"multiplier", "#8fd6ff"},   {"robot_fuel", "#afb6c5"}};
    for (const auto& row : colors)
        if (id == row.first)
            return hexColor(row.second);
    return fallback;
}

LoreLine itemTitle(const Game& game, const Json& item, Color foreground) {
    const auto id = item.get("id", "").asString();
    const Color tint = itemNameColor(game, item, foreground);
    if (id == "pickaxe") {
        auto result = namedText(game.state.get("pick_custom_name", "Prison Tool").asString(), foreground);
        const int tier = game.state.get("parts_prestige", 0).asInt() + 1;
        if (tier > 1) {
            result.push_back({" (", foreground});
            result.push_back({"P" + std::to_string(tier), tint});
            result.push_back({")", foreground});
        }
        return result;
    }
    if (id == "pouch" && !item.get("custom_name", "").asString().empty())
        return namedText(item["custom_name"].asString(), foreground);
    const std::string title = game.itemName(item);
    if (id == "pet_egg")
        return {{title.substr(0, title.size() - 4), tint}, {" Egg", {1, 1, 1, 1}}};
    if (id == "crystal" || id == "crystal_dust" || id == "pick_part" || id == "pouch") {
        const auto split = title.find(' ');
        return {{title.substr(0, split + 1), tint}, {title.substr(split + 1), {1, 1, 1, 1}}};
    }
    return {{title, tint}};
}

std::vector<LoreLine> itemLoreLines(const Game& game, const Json& item, Color foreground) {
    const auto text = game.itemLore(item);
    const auto id = item.get("id", "").asString();
    std::vector<LoreLine> lines;
    for (const auto& line : text)
        lines.push_back({{line, foreground}});
    if (lines.empty())
        return lines;
    lines[0] = itemTitle(game, item, foreground);
    int crystalIndex = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const auto& line = text[i];
        if (id == "pickaxe") {
            if (line.rfind("Level: ", 0) == 0) {
                double ratio = game.state.get("pick_xp", 0).asDouble() /
                               std::max(1.0, game.pickXpNeeded(game.state.get("pick_level", 1).asInt()));
                const int filled = int(std::floor(std::clamp(ratio, 0.0, 1.0) * 12));
                lines[i] = {{line.substr(0, line.find('[') + 1), foreground},
                            {std::string(filled, '|'), hexColor("#55ff55")},
                            {std::string(12 - filled, '|'), hexColor("#ff5555")},
                            {"]", foreground}};
            } else if (line.rfind("> ", 0) == 0) {
                if (line.rfind("> Empty", 0) == 0 || game.crystalSlots() == 0)
                    lines[i] = {{line, hexColor("#919191")}};
                else {
                    const auto& crystal = game.state["equipped_crystals"][crystalIndex];
                    lines[i] = {{"> ", tierColor(crystal.get("tier", 1).asInt())},
                                {line.substr(2), hexColor("#c6c6c6")}};
                }
                ++crystalIndex;
            } else {
                for (const auto& key : game.data.enchants["order"]) {
                    const auto& def = game.data.enchants["defs"][key.asString()];
                    const auto label = def.get("label", key).asString();
                    if (line.rfind(label + ": ", 0) != 0)
                        continue;
                    Color value = hexColor(def.get("value_color", def.get("color", "#c6c6c6")).asString());
                    Color tint = {value.r + (1 - value.r) * .45f, value.g + (1 - value.g) * .45f,
                                  value.b + (1 - value.b) * .45f, 1};
                    tint = hexColor(def.get("label_color", "").asString(), tint);
                    if (game.state.get("enchant_colors_enabled", true).asBool()) {
                        tint = hexColor(game.state.get("enchant_label_color", "").asString(), tint);
                        value = hexColor(game.state.get("enchant_value_color", "").asString(), value);
                    }
                    lines[i] = {{label, tint},
                                {":", hexColor("#555555")},
                                {" ", foreground},
                                {line.substr(label.size() + 2), value}};
                    break;
                }
            }
        } else if (id == "enchant_book" && i == 1)
            lines[i] = {{line, hexColor("#9a82b0")}};
        else if (id == "liquid_xp" && line.rfind("EXP:", 0) == 0)
            lines[i] = splitValue(line, "EXP:", foreground, hexColor("#5796a1"));
        else if (id == "automine_time" && line.rfind("Duration: ", 0) == 0)
            lines[i] = splitValue(line, "Duration: ", foreground, hexColor("#c9705f"));
        else if (id == "multiplier" &&
                 (line.rfind("Duration: ", 0) == 0 || line.rfind("Multiplier: ", 0) == 0))
            lines[i] = splitValue(line, line.substr(0, line.find(':') + 2), foreground, hexColor("#8fd6ff"));
        else if (id == "pouch" && line.rfind("Stored: ", 0) == 0)
            lines[i] = splitValue(line, "Stored: ", foreground, itemNameColor(game, item, foreground));
        else if (id == "pet" && line.rfind("Mutation: ", 0) == 0)
            lines[i] = splitValue(line, "Mutation: ", foreground,
                                  hexColor(game.petMutation(item).get("color", "#ffffff").asString()));
        else if (id == "pet" && line.rfind("Rarity: ", 0) == 0) {
            const auto& tier = rarity(game, item);
            const auto& gradient = tier["gradient"];
            if (gradient.isArray() && !gradient.empty()) {
                lines[i] = {{"Rarity: ", foreground}};
                for (std::size_t c = 8; c < line.size(); ++c)
                    lines[i].push_back(
                        {line.substr(c, 1),
                         hexColor(gradient[unsigned(std::min<std::size_t>(c - 8, gradient.size() - 1))]
                                      .asString())});
            } else
                lines[i] = splitValue(line, "Rarity: ", foreground,
                                      hexColor(tier.get("color", "#44c767").asString()));
        }
    }
    return lines;
}
} // namespace prison
