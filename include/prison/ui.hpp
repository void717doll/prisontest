#pragma once
#include "game.hpp"
#include "lore.hpp"
#include "render.hpp"
#include "settings.hpp"
#include <deque>
namespace prison {
struct UiAction {
    std::string kind;
    std::string value;
    std::string profession;
};
struct ChatLine {
    std::string text;
    Color color;
    bool customColor = false;
    Uint32 expires = 0;
    LoreLine spans;
};
struct UiContext {
    bool playing = false;
    bool resetting = false;
    Cell mineSize{25, 7, 25};
    double fps = 0;
    std::string activityHint;
    double activityProgress = -1;
    Json settings;
    Json worlds;
    std::string worldId;
    std::string buildHash;
    std::deque<ChatLine> messages;
    std::vector<UiAction> actions;
    // negative alpha keeps ordinary text tied to the current theme foreground.
    void say(const std::string& text, Color color = {1, 1, 1, -1}) {
        if (text.empty())
            return;
        messages.push_back({text, color, color.a >= 0, SDL_GetTicks() + 12000, {}});
        while (messages.size() > 100)
            messages.pop_front();
    }
    void say(LoreLine spans) {
        std::string text;
        for (const auto& span : spans)
            text += span.text;
        if (text.empty())
            return;
        say(text);
        messages.back().spans = std::move(spans);
    }
};
class UI {
  public:
    UI(Game& game, Renderer& renderer, UiContext& context);
    ~UI();
    void bind(Game& game);
    void event(const SDL_Event& event);
    void draw();
    bool capturesInput() const;
    std::string menu() const;
    void open(const std::string& page);
    void apply(const Result& result);

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace prison
