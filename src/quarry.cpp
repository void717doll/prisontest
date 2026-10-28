#include "prison/quarry.hpp"

namespace prison {
const std::vector<QuarryCategory>& quarryCategories() {
    static const std::vector<QuarryCategory> categories = {
        {"theme", "Surroundings"}, {"path", "Paths"}, {"lamps", "Lamps"},
        {"flowers", "Flowers"}, {"banner", "Banners"}};
    return categories;
}

const std::vector<QuarryChoice>& quarryChoices(const std::string& category) {
    static const std::vector<QuarryChoice> themes = {
        {"classic", "Classic quarry", 0}, {"sakura", "Sakura grove", 1},
        {"coral", "Coral grove", 3}, {"crystal", "Crystal grove", 5},
        {"frost", "Frost grove", 8}, {"mushroom", "Mushroom grove", 12},
        {"bamboo", "Bamboo grove", 20}, {"desert", "Forsaken desert", 30},
        {"volcanic", "Volcanic grove", 40}, {"willow", "Willow grove", 50},
        {"cursed", "Cursed grove", 75}, {"golden", "Golden grove", 100},
        {"birch", "Birch grove", 125}, {"jungle", "Jungle grove", 150},
        {"redwood", "Redwood grove", 200}, {"baobab", "Baobab grove", 250},
        {"emerald", "Emerald grove", 300}};
    static const std::vector<QuarryChoice> paths = {
        {"classic", "Stone paving", 0}, {"sakura", "Bamboo floor", 1},
        {"coral", "Coral sandstone", 3}, {"crystal", "Crystal stone", 5},
        {"frost", "Snow bricks", 8}, {"mushroom", "Mushroom paving", 12},
        {"bamboo", "Bamboo mosaic", 20}, {"desert", "Chiseled desert stone", 30},
        {"volcanic", "Volcanic rock", 40}, {"willow", "Willow boards", 50},
        {"cursed", "Cursed bricks", 75}, {"golden", "Golden boards", 100},
        {"birch", "White sandstone", 125}, {"jungle", "Mossy cobble", 150},
        {"redwood", "Redwood boards", 200}, {"baobab", "Baobab boards", 250},
        {"emerald", "Mineral stone", 300}};
    static const std::vector<QuarryChoice> lamps = {
        {"classic", "Warm lamps", 0}, {"sakura", "Glowstone", 1},
        {"coral", "Coral lamps", 3}, {"crystal", "Crystal lamps", 5},
        {"frost", "Frost lamps", 8}, {"mushroom", "Mushroom lamps", 12},
        {"bamboo", "Pyrite lanterns", 20}, {"desert", "Amber lamps", 30},
        {"volcanic", "Ember lamps", 40}, {"willow", "Emerald lamps", 50},
        {"cursed", "Weeping lamps", 75}, {"golden", "Polished pyrite", 100},
        {"birch", "Frosted lamps", 125}, {"jungle", "Jungle lanterns", 150},
        {"redwood", "Amber lanterns", 200}, {"baobab", "Savanna lanterns", 250},
        {"emerald", "Emerald crystals", 300}};
    static const std::vector<QuarryChoice> flowers = {
        {"classic", "Meadow flowers", 0}, {"sakura", "Ethereal flowers", 1},
        {"coral", "Coral blooms", 3}, {"crystal", "Crystal lilies", 5},
        {"frost", "Frost plants", 8}, {"mushroom", "Illumishrooms", 12},
        {"bamboo", "Lotus flowers", 20}, {"desert", "Desert plants", 30},
        {"volcanic", "Firethorn flowers", 40}, {"willow", "Willow lotus", 50},
        {"cursed", "Cursed blooms", 75}, {"golden", "Golden meadow", 100},
        {"birch", "White flowers", 125}, {"jungle", "Jungle blooms", 150},
        {"redwood", "Red shrubs", 200}, {"baobab", "Savanna plants", 250},
        {"emerald", "Glowing roots", 300}};
    static const std::vector<QuarryChoice> banners = {
        {"classic", "Rank banners", 0}, {"sakura", "Blue marble", 1},
        {"coral", "Carved coral", 3}, {"crystal", "Carved crystal", 5},
        {"frost", "Frozen crystal", 8}, {"mushroom", "Spotted mushroom", 12},
        {"bamboo", "Bamboo bundles", 20}, {"desert", "Desert engraving", 30},
        {"volcanic", "Ember crystal", 40}, {"willow", "Willow rings", 50},
        {"cursed", "Cursed carvings", 75}, {"golden", "Golden spirals", 100},
        {"birch", "Birch rings", 125}, {"jungle", "Banana rings", 150},
        {"redwood", "Redwood rings", 200}, {"baobab", "Baobab rings", 250},
        {"emerald", "Ancient emerald", 300}};
    static const std::vector<QuarryChoice> empty;
    if (category == "theme")
        return themes;
    if (category == "path")
        return paths;
    if (category == "lamps")
        return lamps;
    if (category == "flowers")
        return flowers;
    if (category == "banner")
        return banners;
    return empty;
}

Json sanitizeQuarryStyle(const Json& saved, std::int64_t totalPrestige) {
    Json style(::Json::objectValue);
    for (const auto& category : quarryCategories()) {
        style[category.id] = "classic";
        if (!saved.isObject() || !saved[category.id].isString())
            continue;
        const auto id = saved[category.id].asString();
        for (const auto& choice : quarryChoices(category.id))
            if (choice.id == id && choice.prestige <= totalPrestige) {
                style[category.id] = id;
                break;
            }
    }
    return style;
}
} // namespace prison
