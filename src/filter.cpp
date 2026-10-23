#include "items_internal.hpp"
#include <map>

namespace prison {
using namespace item_detail;

namespace {
std::string sourceId(const Json& item) {
    static const std::map<std::string, std::string> names = {
        {"rename_token", "prisontest_renametoken:rename_token"},
        {"crystal", "prisontest:crystal"},
        {"crystal_dust", "prisontest:crystal_dust"},
        {"pick_part", "prisontest:pick_part"},
        {"liquid_xp", "prisontest:liquidxp"},
        {"pet", "prisontest_pets:pet"},
        {"pet_egg", "prisontest_pets:pet_egg"},
        {"pet_food", "prisontest_pets:pet_food"},
        {"multiplier", "prisontest_multipliers:sell_multiplier"},
        {"automine_time", "prisontest_autominer:automine_time"},
        {"robot", "prisontest_robots:robot"},
        {"robot_fuel", "prisontest_robots:robot_fuel"},
        {"enchant_book", "prisontest_enchantbooks:enchant_book"}};
    const std::string name = id(item);
    if (name == "pouch")
        return "prisontest_pouches:" + item.get("tier", "small").asString();
    if (name == "minebomb")
        return "prisontest_minebomb:t" + std::to_string(item.get("tier", 1).asInt());
    auto found = names.find(name);
    return found == names.end() ? name : found->second;
}

std::string cleanPattern(std::string pattern) {
    const auto first = pattern.find_first_not_of(" \t\r\n"), last = pattern.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last - first >= 128)
        return "";
    pattern = pattern.substr(first, last - first + 1);
    if (pattern.find(':') == std::string::npos) {
        Json item;
        item["id"] = pattern;
        pattern = pattern == "pouch"      ? "prisontest_pouches:*"
                  : pattern == "minebomb" ? "prisontest_minebomb:*"
                                          : sourceId(item);
    }
    const auto colon = pattern.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == pattern.size())
        return "";
    const auto valid = [](const std::string& text) {
        return text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") ==
               std::string::npos;
    };
    if (!valid(pattern.substr(0, colon)) ||
        (pattern.substr(colon + 1) != "*" && !valid(pattern.substr(colon + 1))))
        return "";
    return pattern;
}

bool filterActionValid(const std::string& action) {
    return action == "keep" || action == "salvage" || action == "vault" || action == "apply";
}
} // namespace

namespace item_detail {
std::string filterAction(const Json& settings, const Json& item) {
    if (!settings.get("enabled", false).asBool() || id(item) == "pickaxe")
        return "keep";
    const auto full = sourceId(item);
    std::string matched;
    for (const auto& pattern : {full, full.substr(0, full.find(':')) + ":*"}) {
        for (const auto& row : settings["rules"])
            if (row.get("pattern", "").asString() == pattern) {
                matched = row.get("action", "keep").asString();
                break;
            }
        if (!matched.empty())
            break;
    }
    if (settings.get("mode", "whitelist").asString() == "blacklist")
        return matched.empty() ? settings.get("default_action", "salvage").asString() : "keep";
    return matched.empty() ? "keep" : matched;
}
} // namespace item_detail

std::string Game::filterPattern(const Json& item, bool family) const {
    if (!item.isObject() || id(item) == "pickaxe")
        return "";
    const auto pattern = cleanPattern(sourceId(item));
    return family && !pattern.empty() ? pattern.substr(0, pattern.find(':')) + ":*" : pattern;
}

Json Game::filterProfile(const Json& settings) const {
    const Json raw = settings.isObject() ? settings : Json(::Json::objectValue);
    Json result;
    result["enabled"] = raw["enabled"].isBool() && raw["enabled"].asBool();
    result["mode"] = raw["mode"] == "blacklist" ? "blacklist" : "whitelist";
    result["default_action"] =
        raw["default_action"].isString() && filterActionValid(raw["default_action"].asString())
            ? raw["default_action"]
            : Json("salvage");
    result["vault"] = raw["vault"].isNumeric() && std::isfinite(raw["vault"].asDouble())
                          ? int(std::clamp(raw["vault"].asDouble(), 1.0, double(MaxVaultPages)))
                          : 1;
    result["rules"] = Json(::Json::arrayValue);
    if (raw["rules"].isArray())
        for (const auto& row : raw["rules"]) {
            if (!row.isObject() || !row["pattern"].isString() || !row["action"].isString())
                continue;
            const auto pattern = cleanPattern(row["pattern"].asString());
            if (pattern.empty() || !filterActionValid(row["action"].asString()))
                continue;
            bool updated = false;
            for (auto& rule : result["rules"])
                if (rule["pattern"] == pattern) {
                    rule["action"] = row["action"];
                    updated = true;
                    break;
                }
            if (!updated && result["rules"].size() < 64) {
                Json rule;
                rule["pattern"] = pattern;
                rule["action"] = row["action"];
                result["rules"].append(rule);
            }
        }
    return result;
}

Result Game::configureFilter(const std::string& action, const Json& args) {
    if (action == "filter") {
        state["cleaner_settings"] = filterProfile(args.isMember("settings") ? args["settings"] : args);
        return success("Filter updated.");
    }
    if (action == "filter_rule") {
        const auto pattern = cleanPattern(args.get("pattern", "").asString());
        const auto ruleAction = args.get("action", "keep").asString();
        if (pattern.empty() || !filterActionValid(ruleAction))
            return fail("Use an item id or a module:* family.");
        Json settings = filterProfile(state["cleaner_settings"]);
        for (auto& row : settings["rules"])
            if (row["pattern"] == pattern) {
                row["action"] = ruleAction;
                state["cleaner_settings"] = settings;
                return success("Filter rule updated.");
            }
        if (settings["rules"].size() >= 64)
            return fail("The filter already has 64 rules.");
        Json row;
        row["pattern"] = pattern;
        row["action"] = ruleAction;
        settings["rules"].append(row);
        state["cleaner_settings"] = settings;
        return success("Filter rule added.");
    }
    return fail("Unknown item action.");
}
} // namespace prison
