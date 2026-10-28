#pragma once
#include "common.hpp"
#include <SDL.h>
#include <memory>
namespace prison {
class Mine;
class PetCompanion;
struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
Image loadImage(const std::filesystem::path& path);
struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};
struct RenderStats {
    std::size_t terrainVertices = 0, terrainDraws = 0, terrainUploads = 0, culledChunks = 0, uiDraws = 0;
    std::size_t heldUploads = 0;
};
class Renderer {
  public:
    Renderer(SDL_Window* window, const Data& data);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void begin(float guiScale, Color sky = {.055f, .065f, .085f, 1});
    void beginUI();
    void flush();
    void present();
    float width() const;
    float height() const;
    float scale() const;
    void rect(Rect box, Color color);
    void border(Rect box, Color color, float thickness = 1);
    void text(float x, float y, const std::string& value, Color color = {1, 1, 1, 1}, int size = 16,
              TextStyle style = {});
    float textWidth(const std::string& value, int size = 16, TextStyle style = {});
    void image(Rect box, const std::string& path, Color tint = {1, 1, 1, 1});
    void item(Rect box, const Json& value, int partsPrestige = 0, Color glint = {.71f, .42f, 1, 1});
    void clip(Rect box);
    void unclip();
    void world(const Mine& mine, Vec3 eye, float yaw, float pitch, float fov);
    void selection(Cell cell, float progress);
    void guardian(const Json& state, int targetedPart, float progress);
    void companion(const PetCompanion& companion, const Json& pet, float yaw, float pitch);
    void billboard(Vec3 position, float size, const std::string& path, float yaw, float pitch);
    void mineHologram(const Mine& mine, Vec3 eye, float yaw, float pitch, float fov, int rank,
                      const Json& shaft = Json());
    void held(const Json& item, int partsPrestige, const Json& settings, float swing);
    void screenshot(const std::filesystem::path& path);
    const RenderStats& stats() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace prison
