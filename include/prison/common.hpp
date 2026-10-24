#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <json/json.h>
#include <string>
#include <vector>
namespace prison {
using Json = ::Json::Value;
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec3 operator-(Vec3 b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec3 operator*(float v) const {
        return {x * v, y * v, z * v};
    }
};
struct Cell {
    int x = 0, y = 0, z = 0;
};
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};
struct TextStyle {
    bool bold = false, italic = false, underline = false, strikethrough = false;
};
struct Result {
    bool ok = true;
    std::string message;
    std::string menu;
    bool resetMine = false;
    Result(bool success = true, std::string text = {}, std::string page = {}, bool reset = false)
        : ok(success), message(std::move(text)), menu(std::move(page)), resetMine(reset) {}
};
struct Proc {
    std::string id;
    int level = 0;
};
struct MiningReward {
    double money = 0, tokens = 0, xp = 0;
    Json context;
    std::vector<Proc> procs;
    bool ok = true;
};
struct Data {
    std::filesystem::path root;
    Json balance, ranks, blocks, enchants, crystals, systems, mine, help;
    void load(const std::filesystem::path& sourceRoot);
};
Json readJson(const std::filesystem::path& path, std::size_t maximum = 4 * 1024 * 1024);
std::string jsonText(const Json& value);
void replaceFile(const std::filesystem::path& source, const std::filesystem::path& destination);
std::string compact(double number);
Color hexColor(const std::string& value, Color fallback = {1, 1, 1, 1});
Color currencyColor(const Data& data, const std::string& id);
Color tierColor(int tier);
} // namespace prison
