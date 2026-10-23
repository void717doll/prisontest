#include "prison/game.hpp"

namespace prison {
void Game::loadTutorial() {
    const Json old = state["tutorial"].isObject() ? state["tutorial"] : Json(::Json::objectValue);
    Json value;
    std::string status = old["status"].isString() ? old["status"].asString() : "skipped";
    if (status != "new" && status != "active" && status != "completed" && status != "skipped")
        status = "skipped";
    value["status"] = status;
    value["mined"] = old["mined"].isInt() ? std::clamp(old["mined"].asInt(), 0, 10) : 0;
    for (const char* event : {"inventory", "enchanted", "ranked"})
        value[event] = old[event].isBool() && old[event].asBool();
    state["tutorial"] = std::move(value);
    if (status == "active" && tutorialStep() == 5)
        state["tutorial"]["status"] = "completed";
}

int Game::tutorialStep() const {
    const auto& value = state["tutorial"];
    if (value.get("mined", 0).asInt() < 10)
        return 1;
    if (value.get("inventory", false) != true)
        return 2;
    if (value.get("enchanted", false) != true)
        return 3;
    if (value.get("ranked", false) != true)
        return 4;
    return 5;
}

void Game::tutorialEvent(const std::string& event) {
    auto& value = state["tutorial"];
    if (value.get("status", "skipped") != "active")
        return;
    if (event == "mined")
        value["mined"] = std::min(10, value.get("mined", 0).asInt() + 1);
    else if (event == "inventory" || event == "enchanted" || event == "ranked")
        value[event] = true;
    if (tutorialStep() == 5) {
        value["status"] = "completed";
        state["_tutorial_finished"] = true;
    }
}

Result Game::tutorialAction(const std::string& action) {
    if (action.empty())
        return {true, "", "tutorial"};
    if (action == "skip") {
        state["tutorial"]["status"] = "skipped";
        state.removeMember("_tutorial_finished");
        return {true, "Tutorial skipped. Use /tutorial to restart it.", "tutorial:close"};
    }
    if (action == "start" || action == "restart") {
        if (action == "restart" || state["tutorial"].get("status", "skipped") != "active") {
            state["tutorial"] = Json();
            loadTutorial();
        }
        state["tutorial"]["status"] = "active";
        state.removeMember("_tutorial_finished");
        return {true, "", "tutorial:close"};
    }
    return {false, "Usage: /tutorial [start|skip|restart]"};
}
} // namespace prison
