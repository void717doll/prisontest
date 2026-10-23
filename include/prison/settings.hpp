#pragma once
#include "common.hpp"

namespace prison {
Json defaultSettings();
Json loadSettings(const std::filesystem::path& path);
void saveSettings(const std::filesystem::path& path, const Json& settings);
} // namespace prison
