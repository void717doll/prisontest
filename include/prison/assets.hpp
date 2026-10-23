#pragma once
#include <filesystem>

namespace prison {
std::filesystem::path updateAssets(const std::filesystem::path& bundle, const std::filesystem::path& cache);
} // namespace prison
