#include "commands_internal.hpp"
#include "prison/game.hpp"
#include <cctype>
#include <map>
#include <sstream>

namespace prison {
namespace command_detail {
Result error(const std::string& text) {
    return {false, text, {}};
}
Result ok(const std::string& text) {
    return {true, text, {}};
}
std::string lower(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool number(const std::string& text, double& out, double low, double high, bool whole) {
    try {
        std::size_t end = 0;
        out = std::stod(text, &end);
        return end == text.size() && std::isfinite(out) && out >= low && out <= high &&
               (!whole || out == std::floor(out));
    } catch (const std::exception&) {
        return false;
    }
}
double numeric(const std::string& text, double low, double high, bool whole) {
    double out = 0;
    return number(text, out, low, high, whole) ? out : -1;
}
std::string join(const std::vector<std::string>& values, std::size_t start) {
    std::string text;
    for (std::size_t i = start; i < values.size(); ++i) {
        if (i > start)
            text += ' ';
        text += values[i];
    }
    return text;
}
} // namespace command_detail

using namespace command_detail;

namespace {
const std::map<std::string, std::string> aliases = {{"sc", "scoreboard"},
                                                    {"cleaner", "filter"},
                                                    {"am", "autominer"},
                                                    {"automine", "autominer"},
                                                    {"scrap", "salvage"},
                                                    {"trash", "salvage"},
                                                    {"bin", "salvage"},
                                                    {"ps", "pointshop"},
                                                    {"shop", "pointshop"},
                                                    {"rm", "resetmine"},
                                                    {"bal", "balance"},
                                                    {"wiki", "help"},
                                                    {"info", "help"},
                                                    {"statistics", "stats"},
                                                    {"mods", "help"},
                                                    {"giverename", "grantrename"},
                                                    {"grantcrystal", "givecrystal"},
                                                    {"grantpart", "givepart"},
                                                    {"grantbook", "givebook"},
                                                    {"grantpetegg", "giveegg"},
                                                    {"grantegg", "giveegg"},
                                                    {"givepetegg", "giveegg"},
                                                    {"grantpet", "givepet"},
                                                    {"grantliquidxp", "givexp"},
                                                    {"grantlquidxp", "givexp"},
                                                    {"giveliquidxp", "givexp"},
                                                    {"granttime", "givetime"},
                                                    {"grantmulti", "givemultiplier"},
                                                    {"grantmultiplier", "givemultiplier"},
                                                    {"givesellmultiplier", "givemultiplier"},
                                                    {"grantbomb", "givebomb"},
                                                    {"grantbag", "givepouch"},
                                                    {"givebag", "givepouch"},
                                                    {"picklvl", "picklevel"}};
const std::map<std::string, std::string> menus = {{"inventory", "inventory"},
                                                  {"robots", "robots"},
                                                  {"mines", "mines"},
                                                  {"mineupgrades", "mine_upgrades"},
                                                  {"sets", "equipment_sets"},
                                                  {"shaft", "shaft"},
                                                  {"contracts", "contracts"},
                                                  {"expeditions", "expeditions"},
                                                  {"pickaxe", "pick"},
                                                  {"enchant", "enchants"},
                                                  {"autominer", "autominer"},
                                                  {"settings", "settings"},
                                                  {"stats", "statistics"},
                                                  {"pets", "pets"},
                                                  {"hatchery", "pets"},
                                                  {"parts", "parts"},
                                                  {"crystals", "crystals"},
                                                  {"resonance", "resonance"},
                                                  {"guardian", "guardian"},
                                                  {"boosts", "boosts"},
                                                  {"rewards", "rewards"},
                                                  {"pv", "vault"},
                                                  {"salvage", "salvage"},
                                                  {"pointshop", "pointshop"},
                                                  {"filter", "filter"},
                                                  {"petmerge", "petmerge"},
                                                  {"combinemulti", "combinemulti"}};
} // namespace

Result Game::command(const std::string& input) {
    if (input.size() > 2048)
        return error("Command is too long.");
    std::istringstream stream(input);
    std::string name;
    stream >> name;
    if (!name.empty() && name[0] == '/')
        name.erase(0, 1);
    name = lower(name);
    if (aliases.count(name))
        name = aliases.at(name);
    std::vector<std::string> args;
    std::string word;
    while (stream >> word)
        args.push_back(word);
    if (name.empty())
        return error("Type /help for commands.");
    if (name == "tutorial") {
        if (args.size() > 1)
            return error("Usage: /tutorial [start|skip|restart]");
        return tutorialAction(args.empty() ? "" : lower(args[0]));
    }
    if (name == "help") {
        const auto search = lower(join(args));
        if (search.size() > 128)
            return error("Search must be 128 characters or fewer.");
        state["_help_search"] = search;
        return {true, "", "help"};
    }
    if (name == "autominer" && !args.empty()) {
        const auto sub = lower(args[0]);
        if (args.size() == 1 && (sub == "start" || sub == "join"))
            return itemAction("autominer_start");
        if (args.size() == 1 && (sub == "stop" || sub == "leave"))
            return itemAction("autominer_stop");
        if (args.size() == 1 && sub == "upgrade")
            return itemAction("autominer_upgrade");
        if (args.size() == 2 && sub == "bps") {
            double value = numeric(args[1], 1, 10);
            if (value >= 0) {
                Json fields;
                fields["value"] = int(value);
                return itemAction("autominer_bps", fields);
            }
        }
        return error("Usage: /autominer [start|stop|upgrade|bps <1-10>]");
    }
    if (name == "pv") {
        const int page = args.empty() ? 1 : int(numeric(args[0], 1, MaxVaultPages));
        if (args.size() > 1 || page < 1)
            return error("Usage: /pv [page]");
        if (page > vaultPages())
            return error("You own " + std::to_string(vaultPages()) + " vault pages. Buy more in /pv.");
        return {true, "", "vault:" + std::to_string(page - 1)};
    }
    if (name == "guardian" && args.size() == 1 && lower(args[0]) == "leave") {
        state["guardian"]["active"] = false;
        return ok("Guardian fight left.");
    }
    if (menus.count(name)) {
        if (!args.empty())
            return error("Usage: /" + name);
        return {true, "", menus.at(name)};
    }
    if (name == "balance") {
        if (!args.empty())
            return error("Usage: /balance");
        return ok("Money: " + compact(state.get("money", 0).asDouble()) +
                  " | Tokens: " + compact(state.get("tokens", 0).asDouble()) +
                  " | Rubys: " + compact(state.get("rubies", 0).asDouble()) +
                  " | Sapphires: " + compact(state.get("sapphires", 0).asDouble()) +
                  " | Points: " + compact(state.get("points", 0).asDouble()));
    }
    if (name == "rank") {
        if (!args.empty())
            return error("Usage: /rank");
        return ok("Rank: " + state.get("rank_name", "A").asString() + " | Prestige: P" +
                  std::to_string(state.get("prestige_level", 0).asInt()));
    }
    if (name == "rankup" || name == "rankupmax") {
        if (args.size() > 1 || (!args.empty() && (name == "rankupmax" || lower(args[0]) != "max")))
            return error("Usage: /rankup [max]");
        return rankUp(name == "rankupmax" || !args.empty());
    }
    if (name == "prestige") {
        if (!args.empty())
            return error("Usage: /prestige");
        return prestige(false);
    }
    if (name == "resetmine") {
        if (inShaft())
            return error("Use /shaft to finish the floor or return to your mine.");
        if (!args.empty())
            return error("Usage: /resetmine");
        double now = state.get("playtime_seconds", 0).asDouble();
        if (now < state.get("resetmine_ready_seconds", 0).asDouble())
            return error("Wait a moment before resetting again.");
        state["resetmine_ready_seconds"] =
            now + data.balance.get("resetmine_cooldown_seconds", 10).asDouble();
        return {true, "Mine reset.", "", true};
    }
    if (name == "clear") {
        if (!args.empty())
            return error("Usage: /clear");
        return {true, "", "clear_chat"};
    }
    if (name == "endmulti") {
        if (!args.empty())
            return error("Usage: /endmulti");
        return itemAction("endmulti");
    }
    if (name == "rename") {
        if (!args.empty())
            return error("Usage: /rename");
        return itemAction("rename_begin");
    }
    if (name == "scoreboard") {
        if (args.size() > 1)
            return error("Usage: /scoreboard [on|off|toggle]");
        const std::string value = args.empty() ? "toggle" : lower(args[0]);
        if (value != "on" && value != "off" && value != "toggle")
            return error("Usage: /scoreboard [on|off|toggle]");
        bool visible =
            value == "toggle" ? !state["settings"].get("show_scoreboard", true).asBool() : value == "on";
        state["settings"]["show_scoreboard"] = visible;
        return ok(visible ? "Scoreboard shown." : "Scoreboard hidden.");
    }
    if (name == "enchantcolor") {
        if (args.size() == 1) {
            const auto value = lower(args[0]);
            if (value == "on" || value == "off" || value == "true" || value == "false") {
                state["enchant_colors_enabled"] = value == "on" || value == "true";
                return ok("Enchant colors updated.");
            }
        }
        if (args.size() != 2)
            return error("Usage: /enchantcolor <#RRGGBB> <#RRGGBB> or <on|off|true|false>");
        for (const auto& value : args) {
            if (value.size() != 7 || value[0] != '#')
                return error("Use a color such as #55ff55.");
            for (std::size_t i = 1; i < value.size(); ++i)
                if (!std::isxdigit(static_cast<unsigned char>(value[i])))
                    return error("Use a color such as #55ff55.");
        }
        state["enchant_label_color"] = lower(args[0]);
        state["enchant_value_color"] = lower(args[1]);
        state["enchant_colors_enabled"] = true;
        return ok("Enchant colors updated.");
    }
    return developmentCommand(name, args);
}
} // namespace prison
