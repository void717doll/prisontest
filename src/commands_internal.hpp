#pragma once
#include "prison/common.hpp"

namespace prison::command_detail {
Result error(const std::string& text);
Result ok(const std::string& text = "");
std::string lower(std::string text);
bool number(const std::string& text, double& out, double low, double high, bool whole = true);
double numeric(const std::string& text, double low, double high, bool whole = true);
std::string join(const std::vector<std::string>& values, std::size_t start = 0);
} // namespace prison::command_detail
