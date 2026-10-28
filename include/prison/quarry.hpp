#pragma once
#include "common.hpp"

namespace prison {
struct QuarryCategory {
    std::string id, label;
};
struct QuarryChoice {
    std::string id, label;
    std::int64_t prestige;
};

const std::vector<QuarryCategory>& quarryCategories();
const std::vector<QuarryChoice>& quarryChoices(const std::string& category);
Json sanitizeQuarryStyle(const Json& saved, std::int64_t totalPrestige);
} // namespace prison
