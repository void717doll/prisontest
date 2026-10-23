#pragma once
#include "prison/game.hpp"

namespace prison::item_detail {
inline constexpr const char* partNames[] = {"head", "binding", "rod"};

Result fail(const std::string& message);
Result success(const std::string& message = "");
int count(const Json& item);
std::string id(const Json& item);
bool insert(Json& slots, const Json& item, int start = 0);
void consume(Json& slots, int index, int amount = 1);
std::string uid(Game& game, const std::string& prefix);
std::string rewardKind(std::string value);
bool apply(Game& game, const Json& item);
bool salvage(Game& game, const Json& item);
std::string filterAction(const Json& settings, const Json& item);
} // namespace prison::item_detail
