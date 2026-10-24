#include "prison/common.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace prison {
Json readJson(const std::filesystem::path& path, std::size_t maximum) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maximum)
        throw std::runtime_error("Cannot read JSON: " + path.string());
    std::ifstream input(path);
    ::Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["rejectDupKeys"] = true;
    builder["stackLimit"] = 64;
    Json result;
    std::string errors;
    if (!::Json::parseFromStream(builder, input, &result, &errors))
        throw std::runtime_error("Invalid JSON: " + path.string() + ": " + errors);
    return result;
}
std::string jsonText(const Json& value) {
    ::Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return ::Json::writeString(b, value);
}
void replaceFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "Could not replace file");
#else
    std::filesystem::rename(source, destination);
#endif
}
void Data::load(const std::filesystem::path& sourceRoot) {
    root = sourceRoot;
    balance = readJson(root / "data/balance.json");
    ranks = readJson(root / "data/ranks.json");
    blocks = readJson(root / "data/blocks.json");
    enchants = readJson(root / "data/enchants.json");
    crystals = readJson(root / "data/crystals.json");
    systems = readJson(root / "data/source_systems.json");
    const auto economy = readJson(root / "data/native_economy.json");
    const auto content = readJson(root / "data/native_content.json");
    if (systems["data_revision"] != 2 || economy["data_revision"] != 2 || content["data_revision"] != 2)
        throw std::runtime_error("outdated gameplay data; update data/source_systems.json, "
                                 "data/native_economy.json and data/native_content.json together");
    systems["currencies"] = economy["currencies"];
    systems["robots"] = economy["robots"];
    systems["endgame"] = economy["endgame"];
    for (const auto& id : economy["defs"].getMemberNames())
        for (const auto& key : economy["defs"][id].getMemberNames())
            enchants["defs"][id][key] = economy["defs"][id][key];
    for (const auto& id : economy["order"])
        enchants["order"].append(id);
    for (const auto& id : economy["effects"].getMemberNames())
        enchants["effects"][id] = economy["effects"][id];
    enchants["defs"]["token_greed"]["color"] = systems["currencies"]["tokens"]["color"];
    for (const char* key : {"resonance", "mutations", "guardians"})
        systems[key] = content[key];
    mine = readJson(root / "data/mine.json");
    help = readJson(root / "data/help.json");
}
std::string compact(double n) {
    if (!std::isfinite(n))
        return "0";
    const char* suffix[] = {"", "K", "M", "B", "T", "Qa", "Qi", "Sx", "Sp", "Oc", "No", "Dc"};
    int i = 0;
    while (std::abs(n) >= 1000 && i < 11) {
        n /= 1000;
        ++i;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(i ? 2 : 0) << n << suffix[i];
    return out.str();
}
Color hexColor(const std::string& v, Color fallback) {
    std::string s = v;
    if (s.size() == 7 && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return fallback;
    unsigned long n = std::stoul(s, nullptr, 16);
    return {float((n >> 16) & 255) / 255, float((n >> 8) & 255) / 255, float(n & 255) / 255, 1};
}
Color currencyColor(const Data& data, const std::string& id) {
    return hexColor(data.systems["currencies"][id].get("color", "#ffffff").asString());
}
Color tierColor(int tier) {
    static const char* colors[] = {"#e6e6e6", "#6f6f6f", "#ff4d4d", "#ff9f1c", "#ffe100", "#7ed957",
                                   "#00d084", "#00c2ff", "#3b82f6", "#6d5dfc", "#b84dff", "#ff4db8"};
    tier = std::max(1, tier);
    Color color;
    if (tier <= 12) {
        color = hexColor(colors[tier - 1]);
    } else {
        const int offset = tier - 12;
        const double hue = std::fmod(offset * 137.508, 360.0) / 60;
        const float value = offset % 2 == 0 ? 1.f : .72f;
        const float chroma = value * .9f;
        const float x = chroma * (1 - std::abs(std::fmod(hue, 2.0) - 1));
        switch (int(hue)) {
        case 0:
            color = {chroma, x, 0, 1};
            break;
        case 1:
            color = {x, chroma, 0, 1};
            break;
        case 2:
            color = {0, chroma, x, 1};
            break;
        case 3:
            color = {0, x, chroma, 1};
            break;
        case 4:
            color = {x, 0, chroma, 1};
            break;
        default:
            color = {chroma, 0, x, 1};
            break;
        }
        color.r = std::round((color.r + value - chroma) * 255) / 255;
        color.g = std::round((color.g + value - chroma) * 255) / 255;
        color.b = std::round((color.b + value - chroma) * 255) / 255;
    }
    const double luminosity = 255 * (.2126 * color.r + .7152 * color.g + .0722 * color.b);
    if (luminosity < 155) {
        const float boost = std::floor((155 - luminosity) * .9) / 255;
        color.r = std::min(1.f, color.r + boost);
        color.g = std::min(1.f, color.g + boost);
        color.b = std::min(1.f, color.b + boost);
    }
    return color;
}
} // namespace prison
