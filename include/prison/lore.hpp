#pragma once
#include "game.hpp"

namespace prison {
struct TextSpan {
    std::string text;
    Color color;
    TextStyle style{};
};
using LoreLine = std::vector<TextSpan>;
LoreLine namedText(const std::string& value, Color foreground);
std::string plainName(const std::string& value);
LoreLine currencyText(const Data& data, const std::string& text, Color foreground);
LoreLine miningSummaryText(const Data& data, const Json& summary);
LoreLine nextRankText(const Game& game);
Color itemNameColor(const Game& game, const Json& item, Color fallback);
LoreLine itemTitle(const Game& game, const Json& item, Color foreground);
std::vector<LoreLine> itemLoreLines(const Game& game, const Json& item, Color foreground);
} // namespace prison
