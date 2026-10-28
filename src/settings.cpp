#include "prison/settings.hpp"
#include <SDL_keycode.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace prison {
Json defaultSettings() {
    Json settings;
    settings["background"] = "#242428";
    settings["foreground"] = "#dddddf";
    settings["border"] = "#68686f";
    settings["glint"] = "#b56cff";
    settings["gui_scale"] = 1.0;
    settings["master_volume"] = 0.75;
    settings["mouse_sensitivity"] = 1.0;
    settings["fov"] = 72.0;
    settings["hand_fov"] = 72.0;
    settings["held_item_scale"] = 1.0;
    settings["tool_angle"] = 90.0;
    settings["scoreboard_opacity"] = 0.75;
    settings["chat_opacity"] = 0.45;
    settings["show_fps"] = false;
    settings["vsync"] = true;
    settings["fullscreen"] = false;
    settings["show_inventory_icon"] = true;
    settings["show_pet_companion"] = true;
    settings["inventory_key"] = int(SDLK_e);
    settings["pickaxe_key"] = int(SDLK_m);
    settings["show_max_labels"] = true;
    settings["left_handed"] = false;
    settings["new_devmode"] = false;
    settings["hud"] = Json(::Json::objectValue);
    return settings;
}

Json loadSettings(const std::filesystem::path& path) {
    Json settings = defaultSettings();
    if (std::filesystem::exists(path)) {
        try {
            const auto stored = readJson(path, 65536);
            if (stored.isObject()) {
                for (const auto& key : stored.getMemberNames()) {
                    if (!settings.isMember(key) || stored[key].type() != settings[key].type())
                        continue;
                    settings[key] = stored[key];
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Settings: " << e.what() << '\n';
        }
    }
    const double savedSensitivity = settings["mouse_sensitivity"].asDouble();
    settings["mouse_sensitivity"] =
        std::isfinite(savedSensitivity) ? std::clamp(savedSensitivity, 0.1, 3.0) : 1.0;
    return settings;
}

void saveSettings(const std::filesystem::path& path, const Json& settings) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary);
        output << jsonText(settings);
        if (!output)
            throw std::runtime_error("Cannot save settings");
    }
    replaceFile(temporary, path);
}
} // namespace prison
