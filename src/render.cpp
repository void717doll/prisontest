#include "prison/render.hpp"
#include "prison/professions.hpp"
#include "prison/guardians.hpp"
#include "prison/world.hpp"
#include <SDL_opengl.h>
#include <ft2build.h>
#include <iostream>
#include FT_FREETYPE_H
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <png.h>
#include <regex>
#include <stdexcept>
#include <unordered_map>
namespace prison {
namespace {
struct FileCloser {
    void operator()(FILE* file) const {
        std::fclose(file);
    }
};
std::unique_ptr<FILE, FileCloser> imageFile(const std::filesystem::path& path, bool write) {
#ifdef _WIN32
    FILE* file = _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
    FILE* file = std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
    return std::unique_ptr<FILE, FileCloser>(file);
}
} // namespace
Image loadImage(const std::filesystem::path& path) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    auto file = imageFile(path, false);
    if (!file || !png_image_begin_read_from_stdio(&png, file.get()))
        throw std::runtime_error("PNG: " + path.string());
    if (png.width > 4096 || png.height > 4096) {
        png_image_free(&png);
        throw std::runtime_error("Image too large");
    }
    png.format = PNG_FORMAT_RGBA;
    Image i;
    i.width = png.width;
    i.height = png.height;
    i.pixels.resize(PNG_IMAGE_SIZE(png));
    if (!png_image_finish_read(&png, nullptr, i.pixels.data(), 0, nullptr)) {
        std::string error = png.message;
        png_image_free(&png);
        throw std::runtime_error(error);
    }
    png_image_free(&png);
    return i;
}
namespace {
GLuint upload(const Image& im) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.width, im.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 im.pixels.data());
    return id;
}
void color(Color c) {
    glColor4f(c.r, c.g, c.b, c.a);
}
void quad(Rect r, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0);
    glVertex2f(r.x, r.y);
    glTexCoord2f(u1, v0);
    glVertex2f(r.x + r.w, r.y);
    glTexCoord2f(u1, v1);
    glVertex2f(r.x + r.w, r.y + r.h);
    glTexCoord2f(u0, v1);
    glVertex2f(r.x, r.y + r.h);
    glEnd();
}
std::vector<unsigned> codepoints(const std::string& s) {
    std::vector<unsigned> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned c = (unsigned char)s[i++];
        int n = 0;
        if (c >= 240) {
            c &= 7;
            n = 3;
        } else if (c >= 224) {
            c &= 15;
            n = 2;
        } else if (c >= 192) {
            c &= 31;
            n = 1;
        }
        while (n-- && i < s.size()) {
            unsigned b = (unsigned char)s[i++];
            if ((b & 192) != 128) {
                c = '?';
                break;
            }
            c = (c << 6) | (b & 63);
        }
        out.push_back(c);
    }
    return out;
}
Color tier(int n) {
    return tierColor(n);
}
void tint(Image& i, Color c, float amount, bool neutralOnly = false) {
    for (size_t p = 0; p < i.pixels.size(); p += 4) {
        float mx = std::max({i.pixels[p], i.pixels[p + 1], i.pixels[p + 2]}),
              mn = std::min({i.pixels[p], i.pixels[p + 1], i.pixels[p + 2]});
        if (neutralOnly && mx > 0 && (mx - mn) / mx >= .18f)
            continue;
        for (int k = 0; k < 3; ++k) {
            float v = k == 0 ? c.r : k == 1 ? c.g : c.b;
            i.pixels[p + k] = static_cast<unsigned char>(i.pixels[p + k] * (1 - amount) + v * 255 * amount);
        }
    }
}
void drawBox(const GuardianPart& part, Color tint) {
        const float low[] = {part.low.x, part.low.y, part.low.z};
        const float high[] = {part.high.x, part.high.y, part.high.z};
        for (int axis = 0; axis < 3; ++axis)
            for (int side = 0; side < 2; ++side) {
                const float shade = axis == 1 ? (side ? 1.f : .55f) : axis == 0 ? .75f : .9f;
                color({tint.r * shade, tint.g * shade, tint.b * shade, tint.a});
                for (int i = 0; i < 4; ++i) {
                    float vertex[] = {low[0], low[1], low[2]};
                    vertex[axis] = side ? high[axis] : low[axis];
                    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                    if (i == 1 || i == 2)
                        vertex[u] = high[u];
                    if (i >= 2)
                        vertex[v] = high[v];
                    glTexCoord2f(i == 1 || i == 2 ? 1 : 0, i >= 2 ? 1 : 0);
                    glVertex3fv(vertex);
                }
            }
}
void perspective(float fov, float aspect, float nearPlane = .05f, float farPlane = 160) {
    float top = nearPlane * std::tan(fov * 3.14159265f / 360);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-top * aspect, top * aspect, -top, top, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
} // namespace
struct Renderer::Impl {
    SDL_Window* window;
    const Data& data;
    SDL_GLContext context = nullptr;
    int w = 1, h = 1;
    float scale = 1;
    FT_Library ft = nullptr;
    FT_Face face = nullptr;
    std::vector<unsigned char> fontBytes;
    using GenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
    using DeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);
    using BindBuffer = void(APIENTRY*)(GLenum, GLuint);
    using BufferData = void(APIENTRY*)(GLenum, std::ptrdiff_t, const void*, GLenum);
    GenBuffers genBuffers = nullptr;
    DeleteBuffers deleteBuffers = nullptr;
    BindBuffer bindBuffer = nullptr;
    BufferData bufferData = nullptr;
    struct Glyph {
        float u = 0, v = 0, uw = 0, vh = 0;
        int w = 0, h = 0, left = 0, top = 0, advance = 0;
    };
    struct Font {
        GLuint texture = 0;
        std::uint64_t lastUsed = 0;
        int x = 1, y = 1, row = 0;
        std::unordered_map<unsigned, Glyph> glyphs;
    };
    std::map<int, Font> fonts;
    std::uint64_t fontClock = 0;
    struct UiVertex {
        float x, y, u, v;
        Color color;
    };
    struct UiCommand {
        GLuint texture;
        GLint first;
        GLsizei count;
        bool clipped;
        std::array<GLint, 4> scissor;
    };
    std::vector<UiVertex> uiVertices;
    std::vector<UiCommand> uiCommands;
    GLuint uiBuffer = 0;
    bool uiClipped = false;
    std::array<GLint, 4> uiScissor{};
    struct Texture {
        GLuint id = 0;
        Image image;
    };
    std::map<std::string, Texture> textures;
    struct Mesh {
        GLuint vbo = 0;
        uint64_t revision = ~uint64_t(0);
        GLsizei count = 0;
    };
    std::vector<Mesh> terrain;
    Mesh shell, heldMesh;
    GLuint heldTexture = 0;
    GLuint atlas = 0;
    RenderStats stats;
    void flushUi() {
        if (uiVertices.empty())
            return;
        if (!uiBuffer)
            genBuffers(1, &uiBuffer);
        bindBuffer(GL_ARRAY_BUFFER, uiBuffer);
        bufferData(GL_ARRAY_BUFFER, uiVertices.size() * sizeof(UiVertex), uiVertices.data(), GL_STREAM_DRAW);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, sizeof(UiVertex), nullptr);
        glTexCoordPointer(2, GL_FLOAT, sizeof(UiVertex), (void*)(2 * sizeof(float)));
        glColorPointer(4, GL_FLOAT, sizeof(UiVertex), (void*)(4 * sizeof(float)));
        GLuint texture = ~GLuint(0);
        for (const auto& command : uiCommands) {
            if (command.texture != texture) {
                texture = command.texture;
                if (texture) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, texture);
                } else
                    glDisable(GL_TEXTURE_2D);
            }
            if (command.clipped) {
                glEnable(GL_SCISSOR_TEST);
                const auto& box = command.scissor;
                glScissor(box[0], box[1], box[2], box[3]);
            } else
                glDisable(GL_SCISSOR_TEST);
            glDrawArrays(GL_QUADS, command.first, command.count);
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        bindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        stats.uiDraws += uiCommands.size();
        uiCommands.clear();
        uiVertices.clear();
    }
    void uiQuad(GLuint texture, const std::array<UiVertex, 4>& corners) {
        if (uiVertices.size() + 4 > 8192)
            flushUi();
        if (uiCommands.empty() || uiCommands.back().texture != texture ||
            uiCommands.back().clipped != uiClipped || (uiClipped && uiCommands.back().scissor != uiScissor))
            uiCommands.push_back({texture, static_cast<GLint>(uiVertices.size()), 0, uiClipped, uiScissor});
        uiCommands.back().count += 4;
        uiVertices.insert(uiVertices.end(), corners.begin(), corners.end());
    }
    void uiQuad(GLuint texture, Rect box, Color c, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
        if (box.w <= 0 || box.h <= 0 || c.a <= 0)
            return;
        uiQuad(texture, {{{box.x, box.y, u0, v0, c},
                          {box.x + box.w, box.y, u1, v0, c},
                          {box.x + box.w, box.y + box.h, u1, v1, c},
                          {box.x, box.y + box.h, u0, v1, c}}});
    }
    Impl(SDL_Window* wnd, const Data& d) : window(wnd), data(d) {
        context = SDL_GL_CreateContext(window);
        if (!context)
            throw std::runtime_error(SDL_GetError());
        genBuffers = reinterpret_cast<GenBuffers>(SDL_GL_GetProcAddress("glGenBuffers"));
        deleteBuffers = reinterpret_cast<DeleteBuffers>(SDL_GL_GetProcAddress("glDeleteBuffers"));
        bindBuffer = reinterpret_cast<BindBuffer>(SDL_GL_GetProcAddress("glBindBuffer"));
        bufferData = reinterpret_cast<BufferData>(SDL_GL_GetProcAddress("glBufferData"));
        if (!genBuffers || !deleteBuffers || !bindBuffer || !bufferData) {
            SDL_GL_DeleteContext(context);
            throw std::runtime_error("OpenGL buffer support is required");
        }
        std::clog << "OpenGL: " << glGetString(GL_RENDERER) << " (" << glGetString(GL_VERSION) << ")\n";
        SDL_GL_SetSwapInterval(1);
        std::ifstream font(d.root / "fonts/DejaVuSans.ttf", std::ios::binary);
        fontBytes.assign(std::istreambuf_iterator<char>(font), std::istreambuf_iterator<char>());
        if (fontBytes.empty() || FT_Init_FreeType(&ft) ||
            FT_New_Memory_Face(ft, fontBytes.data(), fontBytes.size(), 0, &face))
            throw std::runtime_error("Cannot load font");
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        makeAtlas();
        uiVertices.reserve(8192);
        uiCommands.reserve(256);
    }
    ~Impl() {
        for (auto& t : textures)
            glDeleteTextures(1, &t.second.id);
        for (auto& f : fonts)
            glDeleteTextures(1, &f.second.texture);
        for (auto& m : terrain)
            deleteBuffers(1, &m.vbo);
        deleteBuffers(1, &shell.vbo);
        deleteBuffers(1, &heldMesh.vbo);
        deleteBuffers(1, &uiBuffer);
        glDeleteTextures(1, &atlas);
        if (face)
            FT_Done_Face(face);
        if (ft)
            FT_Done_FreeType(ft);
        SDL_GL_DeleteContext(context);
    }
    Texture& texture(const std::string& path) {
        auto it = textures.find(path);
        if (it != textures.end())
            return it->second;
        Image i = loadImage(data.root / path);
        GLuint id = upload(i);
        return textures.emplace(path, Texture{id, std::move(i)}).first->second;
    }
    Texture& activityTexture(const Json& item, int prestige, Color glint) {
        const std::string id = item.get("id", "").asString();
        const std::string profession = item.get("profession", "mining").asString();
        const std::string species = item.get("species", "wheat").asString();
        const bool enchanted = item.get("enchanted", false) == true;
        const std::string key = "item:activity:" + id + ":" + profession + ":" + species + ":" +
            std::to_string(prestige) + ":" + std::to_string(enchanted) + ":" + std::to_string(glint.r) +
            ":" + std::to_string(glint.g) + ":" + std::to_string(glint.b);
        auto found = textures.find(key);
        if (found != textures.end())
            return found->second;
        Image im;
        im.width = im.height = 16;
        im.pixels.resize(16 * 16 * 4);
        auto pixel = [&](int x, int y, Color c) {
            if (x < 0 || y < 0 || x >= 16 || y >= 16) return;
            auto offset = (y * 16 + x) * 4;
            im.pixels[offset] = static_cast<unsigned char>(c.r * 255);
            im.pixels[offset + 1] = static_cast<unsigned char>(c.g * 255);
            im.pixels[offset + 2] = static_cast<unsigned char>(c.b * 255);
            im.pixels[offset + 3] = 255;
        };
        auto fill = [&](int x, int y, int w, int h, Color c) {
            for (int row = y; row < y + h; ++row)
                for (int col = x; col < x + w; ++col)
                    pixel(col, row, c);
        };
        Color wood = hexColor("#9b7548"), edge = hexColor("#493726"), metal = tier(prestige + 1);
        if (id == "pickaxe") {
            for (int i = 2; i < 14; ++i) {
                pixel(i, 16 - i, edge);
                pixel(i, 15 - i, wood);
            }
            if (profession == "fishing") {
                fill(13, 2, 1, 10, hexColor("#cbc9b4"));
                fill(12, 11, 2, 2, hexColor("#c06250"));
                fill(5, 10, 2, 3, metal);
            } else {
                fill(8, 2, 6, 2, hexColor("#59616b"));
                fill(7, 3, 7, 2, metal);
                fill(7, 5, 2, 2, metal);
            }
        } else if (id == "watering_can") {
            fill(4, 6, 7, 7, hexColor("#658da3"));
            fill(5, 7, 5, 5, hexColor("#9db9c0"));
            fill(11, 7, 3, 2, hexColor("#658da3"));
            fill(13, 5, 2, 3, hexColor("#9db9c0"));
            fill(2, 5, 4, 1, metal);
            fill(1, 6, 1, 5, metal);
            fill(2, 11, 2, 1, metal);
        } else {
            Color c = hexColor("#bfa45b");
            const auto& rows = data.systems["professions"][id == "fish" ? "fishing" : "farming"][id == "fish" ? "fish" : "crops"];
            for (const auto& row : rows)
                if (row["id"] == species)
                    c = hexColor(row["color"].asString());
            if (id == "fish") {
                fill(4, 6, 8, 5, c);
                fill(6, 5, 4, 7, c);
                fill(2, 5, 2, 7, c);
                pixel(10, 7, {0.1f, .12f, .1f, 1});
                fill(5, 10, 4, 1, {c.r * .7f, c.g * .7f, c.b * .7f, 1});
            } else if (species == "wheat") {
                fill(7, 3, 1, 12, wood);
                for (int y = 3; y < 11; y += 3) {
                    fill(5, y, 2, 2, c);
                    fill(8, y + 1, 2, 2, c);
                }
            } else if (species == "carrot") {
                fill(8, 2, 2, 4, hexColor("#5b9543"));
                fill(10, 2, 3, 2, hexColor("#5b9543"));
                for (int i = 0; i < 8; ++i)
                    fill(8 - i / 2, 6 + i, 4 - i / 3, 1, c);
            } else {
                fill(4, 6, 8, 6, c);
                fill(6, 4, 5, 10, c);
                pixel(6, 7, wood);
                pixel(10, 10, wood);
            }
        }
        if (enchanted)
            tint(im, glint, 32.f / 255);
        const GLuint tex = upload(im);
        return textures.emplace(key, Texture{tex, std::move(im)}).first->second;
    }
    Texture& itemTexture(const Json& item, int prestige, Color glint, bool fur = false) {
        std::string id = item.get("id", "").asString();
        if ((id == "pickaxe" && item.get("profession", "mining") != "mining") ||
            id == "fish" || id == "crop" || id == "watering_can")
            return activityTexture(item, prestige, glint);
        int n = item.get("tier", 1).isNumeric() ? item.get("tier", 1).asInt() : 1;
        Color c = {1, 1, 1, 1};
        float amount = 0;
        bool neutral = false, enchanted = false;
        std::string path = "prisontest_paper.png";
        if (id == "pickaxe") {
            path = "prisontest_pickaxe.png";
            c = tier(prestige + 1);
            amount = 125.f / 255;
            neutral = true;
            enchanted = item.get("enchanted", false).asBool();
        } else if (id == "crystal" || id == "crystal_dust" || id == "pick_part") {
            path = "prisontest_" + id + ".png";
            c = tier(n);
            amount = 90.f / 255;
            enchanted = id == "crystal";
            if (id == "pick_part" && item.get("identified", false).asBool()) {
                std::string slot = item.get("slot", "").asString();
                if (slot == "head" || slot == "rod" || slot == "binding")
                    path = "prisontest_pick_part_" + slot + ".png";
            }
        } else if (id == "pet" || id == "pet_egg") {
            path = fur           ? "prisontest_pets_fur.png"
                   : id == "pet" ? "prisontest_pets_pet.png"
                                 : "prisontest_pets_egg.png";
            c = hexColor("#44c767");
            for (const auto& row : data.systems["pets"]["tiers"])
                if (row["id"] == item.get("tier_id", "common"))
                    c = hexColor(row["color"].asString());
            if (id == "pet")
                c = hexColor(item.get("pet_color", "").asString(), c);
            amount = (id == "pet" ? 70.f : 90.f) / 255;
        } else if (id == "robot") {
            path = "prisontest_robot.png";
            const auto kind = item.get("boost_type", "money").asString();
            c = kind == "token" ? currencyColor(data, "tokens")
                                : hexColor(kind == "xp" ? "#66ddff" : "#f0c66a");
            amount = 90.f / 255;
        } else if (id == "robot_fuel")
            path = "prisontest_robot_fuel.png";
        else if (id == "pet_food")
            path = "prisontest_pets_food.png";
        else if (id == "minebomb")
            path = "minebomb.png";
        else if (id == "liquid_xp") {
            path = "liquidxp.png";
            c = hexColor("#5796a1");
            amount = 90.f / 255;
        } else if (id == "pouch") {
            path = "prisontest_pouches_bag.png";
            c = tier(item.get("tier", "small").asString() == "small" ? 1 : item["tier"] == "medium" ? 2 : 3);
            amount = 90.f / 255;
        } else if (id == "rename_token")
            path = "prisontest_rename_token.png";
        else if (id == "enchant_book") {
            path = "prisontest_enchant_book.png";
            enchanted = true;
        } else if (id == "multiplier" || id == "automine_time") {
            c = hexColor(id == "multiplier" ? "#4bbcff" : "#ff5a5a");
            amount = 190.f / 255;
        }
        std::string key = "item:" + path + ":" + std::to_string(c.r) + ":" + std::to_string(c.g) + ":" +
                          std::to_string(c.b) + ":" + std::to_string(amount) + ":" +
                          std::to_string(enchanted) + ":" + std::to_string(glint.r) + ":" +
                          std::to_string(glint.g) + ":" + std::to_string(glint.b);
        auto found = textures.find(key);
        if (found != textures.end())
            return found->second;
        Image im = texture("textures/" + path).image;
        tint(im, c, amount, neutral);
        if (enchanted)
            tint(im, glint, 32.f / 255);
        GLuint tex = upload(im);
        return textures.emplace(key, Texture{tex, std::move(im)}).first->second;
    }
    Font& font(int size) {
        if (!fonts.count(size) && fonts.size() >= 8) {
            flushUi();
            auto oldest =
                std::min_element(fonts.begin(), fonts.end(), [](const auto& left, const auto& right) {
                    return left.second.lastUsed < right.second.lastUsed;
                });
            glDeleteTextures(1, &oldest->second.texture);
            fonts.erase(oldest);
        }
        auto& f = fonts[size];
        f.lastUsed = ++fontClock;
        if (!f.texture) {
            Image blank;
            blank.width = blank.height = 1024;
            blank.pixels.resize(1024 * 1024 * 4);
            f.texture = upload(blank);
        }
        return f;
    }
    Glyph glyph(Font& f, int size, unsigned cp) {
        auto it = f.glyphs.find(cp);
        if (it != f.glyphs.end())
            return it->second;
        FT_Set_Pixel_Sizes(face, 0, size);
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER))
            return {};
        auto g = face->glyph;
        int gw = g->bitmap.width, gh = g->bitmap.rows;
        if (f.x + gw + 1 >= 1024) {
            f.x = 1;
            f.y += f.row + 1;
            f.row = 0;
        }
        if (f.y + gh + 1 >= 1024)
            return {};
        std::vector<unsigned char> rgba(gw * gh * 4, 255);
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x)
                rgba[(y * gw + x) * 4 + 3] = g->bitmap.buffer[y * g->bitmap.pitch + x];
        glBindTexture(GL_TEXTURE_2D, f.texture);
        if (gw && gh)
            glTexSubImage2D(GL_TEXTURE_2D, 0, f.x, f.y, gw, gh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        Glyph result{f.x / 1024.f, f.y / 1024.f,   gw / 1024.f,   gh / 1024.f,           gw,
                     gh,           g->bitmap_left, g->bitmap_top, int(g->advance.x >> 6)};
        f.x += gw + 1;
        f.row = std::max(f.row, gh);
        f.glyphs[cp] = result;
        return result;
    }
    void makeAtlas() {
        Image stone = loadImage(data.root / "textures/prisontest_stone.png"),
              ore = loadImage(data.root / "textures/prisontest_ore.png"),
              bedrock = loadImage(data.root / "textures/prisontest_void.png");
        Image a;
        tint(bedrock, {0, 0, 0, 1}, 170.f / 255);
        a.width = a.height = 128;
        a.pixels.resize(128 * 128 * 4);
        int i = 0;
        std::regex hex("#[0-9a-fA-F]{6}");
        for (const auto& def : data.mine["blocks"]) {
            std::string tile = def.get("tile", "").asString();
            std::vector<Color> colors;
            for (auto it = std::sregex_iterator(tile.begin(), tile.end(), hex); it != std::sregex_iterator();
                 ++it)
                colors.push_back(hexColor(it->str()));
            Color c = colors.empty() ? Color{} : colors[0];
            Image im = stone;
            for (size_t p = 0; p < im.pixels.size(); p += 4) {
                im.pixels[p] *= c.r;
                im.pixels[p + 1] *= c.g;
                im.pixels[p + 2] *= c.b;
            }
            tint(im, hexColor("#0f1218"), 70.f / 255);
            if (def["id"].asString().find("_ore") != std::string::npos) {
                Color oc = colors.size() > 3 ? colors[3] : c;
                for (size_t p = 0; p < im.pixels.size(); p += 4) {
                    float alpha = ore.pixels[p + 3] / 255.f;
                    for (int k = 0; k < 3; ++k) {
                        float v = k == 0 ? oc.r : k == 1 ? oc.g : oc.b;
                        im.pixels[p + k] = static_cast<unsigned char>(
                            im.pixels[p + k] * (1 - alpha) + ore.pixels[p + k] * (.7f + .3f * v) * alpha);
                    }
                }
            }
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    std::memcpy(&a.pixels[(((i / 8) * 16 + y) * 128 + (i % 8) * 16 + x) * 4],
                                &im.pixels[(y * 16 + x) * 4], 4);
            ++i;
        }
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                std::memcpy(&a.pixels[((6 * 16 + y) * 128 + 4 * 16 + x) * 4],
                            &bedrock.pixels[(y * 16 + x) * 4], 4);
        {
            Image im = stone;
            tint(im, hexColor("#171b20"), .78f);
            const Color c = hexColor(data.systems["discoveries"].get("pocket_color", "#83d7e8").asString());
            for (std::size_t p = 0; p < im.pixels.size(); p += 4) {
                const float alpha = ore.pixels[p + 3] / 255.f;
                for (int k = 0; k < 3; ++k) {
                    const float component = k == 0 ? c.r : k == 1 ? c.g : c.b;
                    im.pixels[p + k] = static_cast<unsigned char>(im.pixels[p + k] * (1 - alpha) +
                                                                  (component * 220 + 35) * alpha);
                }
            }
            const int tile = TerrainPocketTile;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    std::memcpy(&a.pixels[(((tile / 8) * 16 + y) * 128 + (tile % 8) * 16 + x) * 4],
                                &im.pixels[(y * 16 + x) * 4], 4);
        }
        const Color outdoorColors[] = {hexColor("#64824e"), hexColor("#6b4d38"), hexColor("#92734a"), hexColor("#45683c"), hexColor("#b5a98c")};
        for (int tile = TerrainGrassTile; tile <= TerrainPathTile; ++tile) {
            Image im = stone;
            tint(im, outdoorColors[tile - TerrainGrassTile], .78f);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    std::memcpy(&a.pixels[(((tile / 8) * 16 + y) * 128 + (tile % 8) * 16 + x) * 4],
                                &im.pixels[(y * 16 + x) * 4], 4);
        }
        atlas = upload(a);
    }
    void mesh(Mesh& gpu, const ChunkMesh& cpu) {
        if (cpu.vertices.empty())
            return;
        if (!gpu.vbo)
            genBuffers(1, &gpu.vbo);
        if (gpu.revision != cpu.revision) {
            bindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
            bufferData(GL_ARRAY_BUFFER, cpu.vertices.size() * sizeof(Vertex), cpu.vertices.data(),
                       GL_DYNAMIC_DRAW);
            gpu.revision = cpu.revision;
            gpu.count = cpu.vertices.size();
            ++stats.terrainUploads;
        }
        drawMesh(gpu);
        stats.terrainVertices += gpu.count;
        ++stats.terrainDraws;
    }
    void drawMesh(const Mesh& gpu, GLenum mode = GL_TRIANGLES) {
        bindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glVertexPointer(3, GL_FLOAT, sizeof(Vertex), nullptr);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));
        glColorPointer(3, GL_FLOAT, sizeof(Vertex), (void*)(5 * sizeof(float)));
        glDrawArrays(mode, 0, gpu.count);
    }
    void heldItem(const Texture& texture) {
        if (heldTexture != texture.id) {
            const auto& im = texture.image;
            constexpr float depth = .0625f;
            std::vector<Vertex> vertices;
            vertices.reserve(1024);
            auto append = [&](const std::array<Vertex, 4>& corners) {
                vertices.insert(vertices.end(), corners.begin(), corners.end());
            };
            for (int side = 0; side < 2; ++side) {
                float z = side == 0 ? depth : -depth, shade = side == 0 ? 1.f : .78f;
                append({{{-.5f, .5f, z, 0, 0, shade, shade, shade},
                         {.5f, .5f, z, 1, 0, shade, shade, shade},
                         {.5f, -.5f, z, 1, 1, shade, shade, shade},
                         {-.5f, -.5f, z, 0, 1, shade, shade, shade}}});
            }
            auto opaque = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < im.width && y < im.height &&
                       im.pixels[(y * im.width + x) * 4 + 3] >= 128;
            };
            for (int y = 0; y < im.height; ++y)
                for (int x = 0; x < im.width; ++x) {
                    if (!opaque(x, y))
                        continue;
                    float x0 = x / float(im.width) - .5f, x1 = (x + 1) / float(im.width) - .5f,
                          y1 = .5f - y / float(im.height), y0 = .5f - (y + 1) / float(im.height);
                    float u = (x + .5f) / im.width, v = (y + .5f) / im.height;
                    for (int side = 0; side < 4; ++side) {
                        if (opaque(x + (side == 0   ? -1
                                        : side == 1 ? 1
                                                    : 0),
                                   y + (side == 2   ? -1
                                        : side == 3 ? 1
                                                    : 0)))
                            continue;
                        const float shade = side == 2 ? 1.f : .8f;
                        std::array<Vertex, 4> corners;
                        for (int i = 0; i < 4; ++i) {
                            float z = i >= 2 ? depth : -depth;
                            if (side < 2)
                                corners[i] = {side == 0 ? x0 : x1,
                                              i == 1 || i == 2 ? y1 : y0,
                                              z,
                                              u,
                                              v,
                                              shade,
                                              shade,
                                              shade};
                            else
                                corners[i] = {i == 1 || i == 2 ? x1 : x0,
                                              side == 2 ? y1 : y0,
                                              z,
                                              u,
                                              v,
                                              shade,
                                              shade,
                                              shade};
                        }
                        append(corners);
                    }
                }
            if (!heldMesh.vbo)
                genBuffers(1, &heldMesh.vbo);
            bindBuffer(GL_ARRAY_BUFFER, heldMesh.vbo);
            bufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
            heldMesh.count = static_cast<GLsizei>(vertices.size());
            heldTexture = texture.id;
            ++stats.heldUploads;
        }
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        drawMesh(heldMesh, GL_QUADS);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        bindBuffer(GL_ARRAY_BUFFER, 0);
    }
};
Renderer::Renderer(SDL_Window* w, const Data& d) : p(std::make_unique<Impl>(w, d)) {}
Renderer::~Renderer() = default;
float Renderer::width() const {
    return p->w / p->scale;
}
float Renderer::height() const {
    return p->h / p->scale;
}
float Renderer::scale() const {
    return p->scale;
}
void Renderer::begin(float scale, Color sky) {
    p->flushUi();
    p->uiClipped = false;
    p->stats = {};
    // evict between frames so a pet's face survives its fur texture lookup.
    if (p->textures.size() > 256) {
        p->heldTexture = 0;
        for (auto it = p->textures.begin(); it != p->textures.end();) {
            if (it->first.rfind("item:", 0) == 0) {
                glDeleteTextures(1, &it->second.id);
                it = p->textures.erase(it);
            } else
                ++it;
        }
    }
    SDL_GL_GetDrawableSize(p->window, &p->w, &p->h);
    p->scale = std::clamp(scale, .5f, 2.f);
    glViewport(0, 0, p->w, p->h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(sky.r, sky.g, sky.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void Renderer::beginUI() {
    p->flushUi();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width(), height(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
void Renderer::present() {
    p->flushUi();
    SDL_GL_SwapWindow(p->window);
}
void Renderer::flush() {
    p->flushUi();
}
void Renderer::rect(Rect r, Color c) {
    p->uiQuad(0, r, c);
}
void Renderer::border(Rect r, Color c, float t) {
    rect({r.x, r.y, r.w, t}, c);
    rect({r.x, r.y + r.h - t, r.w, t}, c);
    rect({r.x, r.y, t, r.h}, c);
    rect({r.x + r.w - t, r.y, t, r.h}, c);
}
float Renderer::textWidth(const std::string& s, int size, TextStyle style) {
    if (s.empty())
        return 0;
    float x = 0;
    int px = std::clamp(int(std::round(size * p->scale)), 8, 48);
    const int weight = style.bold ? std::max(1, px / 16) : 0;
    auto& font = p->font(px);
    for (unsigned cp : codepoints(s))
        x += (p->glyph(font, px, cp).advance + weight) / p->scale;
    return x;
}
void Renderer::text(float x, float y, const std::string& s, Color c, int size, TextStyle style) {
    if (s.empty() || c.a <= 0)
        return;
    int px = std::clamp(int(std::round(size * p->scale)), 8, 48);
    const int weight = style.bold ? std::max(1, px / 16) : 0;
    auto& font = p->font(px);
    float start = x;
    for (unsigned cp : codepoints(s)) {
        if (cp == '\n') {
            x = start;
            y += size + 4;
            continue;
        }
        auto g = p->glyph(font, px, cp);
        const Rect box{std::round((x + g.left / p->scale) * p->scale) / p->scale,
                       std::round((y + (px - g.top) / p->scale) * p->scale) / p->scale, g.w / p->scale,
                       g.h / p->scale};
        for (int offset = 0; g.w > 0 && g.h > 0 && offset <= weight; ++offset) {
            const float left = box.x + offset / p->scale;
            const float topShift = style.italic ? g.top * .2f / p->scale : 0;
            const float bottomShift = style.italic ? (g.top - g.h) * .2f / p->scale : 0;
            p->uiQuad(font.texture, {{{left + topShift, box.y, g.u, g.v, c},
                                      {left + box.w + topShift, box.y, g.u + g.uw, g.v, c},
                                      {left + box.w + bottomShift, box.y + box.h, g.u + g.uw, g.v + g.vh, c},
                                      {left + bottomShift, box.y + box.h, g.u, g.v + g.vh, c}}});
        }
        const float advance = (g.advance + weight) / p->scale;
        if (style.underline)
            rect({x, y + px / p->scale, advance, 1 / p->scale}, c);
        if (style.strikethrough)
            rect({x, y + px * .6f / p->scale, advance, 1 / p->scale}, c);
        x += advance;
    }
}
void Renderer::image(Rect box, const std::string& path, Color c) {
    p->uiQuad(p->texture(path).id, box, c);
}
void Renderer::item(Rect box, const Json& v, int prestige, Color glint) {
    if (!v.isObject())
        return;
    auto& tex = p->itemTexture(v, prestige, glint);
    if (v.get("id", "") == "pet") {
        auto face = tex.id;
        auto fur = p->itemTexture(v, prestige, glint, true).id;
        float x = box.x + box.w * .1f, y = box.y + box.h * .1f, w = box.w * .8f, h = box.h * .8f;
        auto plane = [&](GLuint texture, Color c, std::array<std::array<float, 2>, 4> points) {
            std::array<Impl::UiVertex, 4> corners;
            for (int i = 0; i < 4; ++i)
                corners[i] = {points[i][0], points[i][1], i == 1 || i == 2 ? 1.f : 0.f, i >= 2 ? 1.f : 0.f,
                              c};
            p->uiQuad(texture, corners);
        };
        plane(fur, {1, 1, 1, 1},
              {{{x, y + h * .18f}, {x + w * .53f, y}, {x + w, y + h * .25f}, {x + w * .47f, y + h * .43f}}});
        plane(face, {.78f, .78f, .78f, 1},
              {{{x, y + h * .18f}, {x + w * .47f, y + h * .43f}, {x + w * .47f, y + h}, {x, y + h * .75f}}});
        plane(fur, {.52f, .52f, .52f, 1},
              {{{x + w * .47f, y + h * .43f},
                {x + w, y + h * .25f},
                {x + w, y + h * .82f},
                {x + w * .47f, y + h}}});
    } else
        p->uiQuad(tex.id, box, {1, 1, 1, 1});
    int count = v.get("count", 1).asInt();
    if (count > 1) {
        std::string s = std::to_string(count);
        float x = box.x + box.w - textWidth(s, 17) - 1, y = box.y + box.h - 20;
        text(x + 1, y + 1, s, {0, 0, 0, 1}, 17);
        text(x - 1, y - 1, s, {0, 0, 0, 1}, 17);
        text(x, y, s, {1, 1, 1, 1}, 17);
    }
}
void Renderer::clip(Rect r) {
    p->uiClipped = true;
    p->uiScissor = {int(r.x * p->scale), int(p->h - (r.y + r.h) * p->scale), int(r.w * p->scale),
                    int(r.h * p->scale)};
}
void Renderer::unclip() {
    p->uiClipped = false;
}
void Renderer::world(const Mine& mine, Vec3 eye, float yaw, float pitch, float fov) {
    p->flushUi();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    perspective(fov, float(p->w) / p->h);
    glRotatef(-pitch * 180 / 3.14159265f, 1, 0, 0);
    glRotatef(-yaw * 180 / 3.14159265f, 0, 1, 0);
    glTranslatef(-eye.x, -eye.y, -eye.z);
    glBindTexture(GL_TEXTURE_2D, p->atlas);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (p->terrain.size() != mine.chunks().size()) {
        for (auto& m : p->terrain)
            p->deleteBuffers(1, &m.vbo);
        p->terrain.clear();
        p->terrain.resize(mine.chunks().size());
    }
    const float tangent = std::tan(fov * 3.14159265f / 360);
    const Vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const Vec3 right{std::cos(yaw), 0, -std::sin(yaw)};
    const Vec3 up{std::sin(yaw) * std::sin(pitch), std::cos(pitch), std::cos(yaw) * std::sin(pitch)};
    const Vec3 horizontal = forward * (tangent * float(p->w) / p->h), vertical = forward * tangent;
    const std::array<Vec3, 6> normals = {
        horizontal + right, horizontal - right, vertical + up, vertical - up, forward, forward * -1};
    const float offsets[] = {0, 0, 0, 0, -.05f, 160};
    p->mesh(p->shell, mine.shellMesh());
    for (size_t i = 0; i < p->terrain.size(); ++i) {
        const auto& chunk = mine.chunks()[i];
        if (chunk.vertices.empty())
            continue;
        bool visible = true;
        for (size_t plane = 0; plane < normals.size(); ++plane) {
            const auto n = normals[plane];
            const Vec3 corner{n.x >= 0 ? chunk.maximum.x : chunk.minimum.x,
                              n.y >= 0 ? chunk.maximum.y : chunk.minimum.y,
                              n.z >= 0 ? chunk.maximum.z : chunk.minimum.z};
            const auto relative = corner - eye;
            if (n.x * relative.x + n.y * relative.y + n.z * relative.z + offsets[plane] < -.001f) {
                visible = false;
                break;
            }
        }
        if (visible)
            p->mesh(p->terrain[i], chunk);
        else
            ++p->stats.culledChunks;
    }
    p->bindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}
void Renderer::selection(Cell c, float progress) {
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    color({0, 0, 0, .65f});
    glLineWidth(1);
    float x = c.x - .502f, y = c.y - .502f, z = c.z - .502f;
    glBegin(GL_LINES);
    for (int axis = 0; axis < 3; ++axis)
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b) {
                float v[3] = {x, y, z};
                v[(axis + 1) % 3] += a * 1.004f;
                v[(axis + 2) % 3] += b * 1.004f;
                glVertex3fv(v);
                v[axis] += 1.004f;
                glVertex3fv(v);
            }
    glEnd();
    if (progress <= 0)
        return;
    glEnable(GL_TEXTURE_2D);
    auto& crack = p->texture("textures/mining_crack.png");
    glBindTexture(GL_TEXTURE_2D, crack.id);
    color({1, 1, 1, std::clamp(progress, 0.f, 1.f)});
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -1);
    glBegin(GL_QUADS);
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 4; ++i) {
                float v[3] = {x, y, z};
                v[axis] += side * 1.004f;
                v[(axis + 1) % 3] += (i == 1 || i == 2) * 1.004f;
                v[(axis + 2) % 3] += (i >= 2) * 1.004f;
                glTexCoord2f(i == 1 || i == 2 ? 1 : 0, i >= 2 ? 1 : 0);
                glVertex3fv(v);
            }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
}
void Renderer::guardian(const Json& state, int targetedPart, float progress) {
    if (!state.get("active", false).asBool())
        return;
    const auto parts = guardianParts(state);
    const int weak = guardianWeakPart(state);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, p->texture("textures/prisontest_stone.png").id);
    glBegin(GL_QUADS);
    for (int i = 0; i < int(parts.size()); ++i)
        drawBox(parts[i], i == weak ? Color{.65f, .95f, 1, 1} : Color{.48f, .52f, .59f, 1});
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    const auto& head = parts[3];
    for (int side = 0; side < 2; ++side) {
        const float x = head.low.x + .18f + side * .56f;
        for (float z : {head.low.z - .012f, head.high.z})
            drawBox({{x, head.low.y + .55f, z}, {x + .24f, head.low.y + .72f, z + .012f}}, {.5f, .9f, 1, 1});
    }
    glEnd();
    if (targetedPart == weak && progress > 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, p->texture("textures/mining_crack.png").id);
        glEnable(GL_BLEND);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1, -1);
        glBegin(GL_QUADS);
        drawBox(parts[weak], {1, 1, 1, std::clamp(progress, 0.f, 1.f)});
        glEnd();
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void Renderer::outdoors(const Game& farming, const Game& fishing, Vec3 eye) {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, p->texture("textures/prisontest_stone.png").id);
    glBegin(GL_QUADS);
    const auto& crops = farming.data.systems["professions"]["farming"]["crops"];
    const int owned = farming.state["activity"]["plots_owned"].asInt();
    const bool flash = farming.state.get("_activity_flash", 0).asDouble() > 0;
    for (int i = 0; i < 27; ++i) {
        const auto centre = plotPosition(i);
        const auto& plot = farming.state["activity"]["plots"][i];
        const bool watered = plot.isObject() && plot["watered"] == true;
        Color soil = i >= owned ? Color{.4f, .43f, .32f, 1}
                      : watered ? Color{.3f, .24f, .17f, 1} : Color{.55f, .38f, .24f, 1};
        drawBox({centre + Vec3{-1, 0, -1}, centre + Vec3{1, .08f, 1}}, soil);
        if (i >= owned || !plot.isObject())
            continue;
        const float growth = float(farming.cropProgress(i));
        Color leaf = growth >= 1 ? hexColor("#c9b153") : hexColor("#83a948");
        for (const auto& crop : crops)
            if (crop["id"] == plot["crop"] && growth >= 1)
                leaf = hexColor(crop["color"].asString());
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) {
                Vec3 low = centre + Vec3{-.65f + col * .6f, .08f, -.65f + row * .6f};
                drawBox({low, low + Vec3{.12f, .15f + growth * .65f, .12f}}, {.3f, .5f, .2f, 1});
                low.y += .1f + growth * .3f;
                drawBox({low + Vec3{-.12f, 0, -.12f}, low + Vec3{.25f, .12f + growth * .2f, .25f}},
                        flash ? Color{.8f, .9f, .4f, 1} : leaf);
            }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    color({.2f, .46f, .57f, .8f});
    glBegin(GL_QUADS);
    glVertex3f(7, 1.34f, 80);
    glVertex3f(22, 1.34f, 80);
    glVertex3f(22, 1.34f, 103);
    glVertex3f(7, 1.34f, 103);
    glEnd();
    const auto& cast = fishing.state["_cast"];
    if (cast.isObject()) {
        const float x = cast["x"].asFloat(), z = cast["z"].asFloat();
        const bool bite = cast["phase"] == "bite";
        const float bob = std::sin(SDL_GetTicks() * (bite ? .018f : .004f)) * (bite ? .08f : .025f);
        glBegin(GL_QUADS);
        drawBox({{x - .09f, 1.34f + bob, z - .09f}, {x + .09f, 1.54f + bob, z + .09f}}, {1, .35f, .27f, 1});
        glEnd();
        glLineWidth(1);
        glBegin(GL_LINES);
        color({.9f, .87f, .75f, .8f});
        glVertex3f(eye.x + .3f, eye.y - .35f, eye.z);
        glVertex3f(x, 1.5f + bob, z);
        glEnd();
    }
    if (fishing.state.get("_activity_flash", 0).asDouble() > 0) {
        glBegin(GL_LINES);
        color({.7f, .9f, 1, 1});
        for (int i = 0; i < 12; ++i) {
            float x = 8 + i, z = 91 + std::sin(i * 2.f) * 5;
            glVertex3f(x, 1.4f, z);
            glVertex3f(x + .6f, 1.4f, z);
        }
        glEnd();
    }
}

void Renderer::worldLabel(Vec3 point, Vec3 eye, float yaw, float pitch, float fov, const std::string& value) {
    const Vec3 relative = point - eye;
    if (std::hypot(relative.x, relative.z) > 35)
        return;
    const Vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const float depth = relative.x * forward.x + relative.y * forward.y + relative.z * forward.z;
    if (depth <= .1f)
        return;
    const float horizontal = relative.x * std::cos(yaw) - relative.z * std::sin(yaw);
    const float vertical = relative.x * std::sin(yaw) * std::sin(pitch) + relative.y * std::cos(pitch) +
                           relative.z * std::cos(yaw) * std::sin(pitch);
    const float tangent = std::tan(fov * 3.14159265f / 360);
    const float x = width() * (.5f + horizontal / (depth * tangent * (float(p->w) / p->h)) * .5f);
    const float y = height() * (.5f - vertical / (depth * tangent) * .5f);
    if (x < 0 || x > width() || y < 0 || y > height())
        return;
    const float left = x - textWidth(value, 14) / 2;
    text(left + 1, y + 1, value, {0, 0, 0, 1}, 14);
    text(left, y, value, {.92f, .9f, .78f, 1}, 14);
}

void Renderer::billboard(Vec3 position, float size, const std::string& path, float yaw, float pitch) {
    glEnable(GL_BLEND);
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glRotatef(yaw * 180 / 3.14159265f, 0, 1, 0);
    glRotatef(pitch * 180 / 3.14159265f, 1, 0, 0);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, .1f);
    glBindTexture(GL_TEXTURE_2D, p->texture(path).id);
    color({1, 1, 1, 1});
    quad({-size * .5f, -size * .5f, size, size}, 0, 1, 1, 0);
    glPopMatrix();
}
void Renderer::mineHologram(const Mine& mine, Vec3 eye, float yaw, float pitch, float fov, int rank,
                            const Json& shaft) {
    Vec3 relative = Vec3{0, float(mine.maximum().y + 4), float(mine.maximum().z - 1)} - eye;
    Vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    float depth = relative.x * forward.x + relative.y * forward.y + relative.z * forward.z;
    if (depth <= .05f)
        return;
    float horizontal = relative.x * std::cos(yaw) - relative.z * std::sin(yaw);
    float vertical = relative.x * std::sin(yaw) * std::sin(pitch) + relative.y * std::cos(pitch) +
                     relative.z * std::cos(yaw) * std::sin(pitch);
    float tangent = std::tan(fov * 3.14159265f / 360);
    float x = width() * (.5f + horizontal / (depth * tangent * (float(p->w) / p->h)) * .5f);
    float y = height() * (.5f - vertical / (depth * tangent) * .5f);
    if (x < -210 || x > width() + 210 || y < -50 || y > height() + 50)
        return;
    const auto& definition = p->data.mine["blocks"][std::clamp(rank, 0, 25) * 2];
    std::string tile = definition.get("tile", "").asString();
    auto start = tile.find('#');
    Color tint = start == std::string::npos ? Color{} : hexColor(tile.substr(start, 7));
    std::array<std::string, 2> lines = {"Your Mine",
                                        std::to_string(int(std::round(mine.minedRatio() * 100))) + "% mined"};
    if (shaft.isObject() && shaft.get("active", false) == true) {
        lines[0] = "Shaft Floor " + shaft["floor"]["depth"].asString();
        lines[1] = shaft["progress"].asInt() >= shaft["floor"]["goal"].asInt()
                       ? "Cleared · /shaft to descend"
                       : shaft["progress"].asString() + "/" + shaft["floor"]["goal"].asString() + " blocks";
    }
    for (int line = 0; line < 2; ++line) {
        float left = x - textWidth(lines[line], 18) * .5f;
        float top = y - 23 + line * 23;
        text(left + 2, top + 2, lines[line], {0, 0, 0, 1}, 18);
        text(left, top, lines[line], tint, 18);
    }
}
void Renderer::held(const Json& value, int prestige, const Json& settings, float swing) {
    p->flushUi();
    glEnable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, .1f);
    perspective(settings.get("hand_fov", 72).asFloat(), float(p->w) / p->h);
    // mirror the complete held layer, including the model, arm and swing.
    glScalef(settings.get("left_handed", false).asBool() ? -1.f : 1.f, 1, 1);
    const float motion = std::sin(swing * 3.14159f);
    glTranslatef(.58f - motion * .15f, -.43f - motion * .06f, -1.05f + motion * .06f);
    glRotatef(-motion * 51.57f, 1, 0, 0);
    glRotatef(-motion * 22.92f, 0, 0, 1);
    glRotatef(-8, 0, 1, 0);
    float s = (value.isObject() ? settings.get("held_item_scale", 1).asFloat() : 1.f) * .42f;
    glScalef(s, s, s);
    if (!value.isObject()) {
        glDisable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        const float faces[6][4][3] = {
            {{-.14f, -.8f, .12f}, {.14f, -.8f, .12f}, {.14f, .05f, .12f}, {-.14f, .05f, .12f}},
            {{.14f, -.8f, -.12f}, {-.14f, -.8f, -.12f}, {-.14f, .05f, -.12f}, {.14f, .05f, -.12f}},
            {{-.14f, -.8f, -.12f}, {-.14f, -.8f, .12f}, {-.14f, .05f, .12f}, {-.14f, .05f, -.12f}},
            {{.14f, -.8f, .12f}, {.14f, -.8f, -.12f}, {.14f, .05f, -.12f}, {.14f, .05f, .12f}},
            {{-.14f, .05f, .12f}, {.14f, .05f, .12f}, {.14f, .05f, -.12f}, {-.14f, .05f, -.12f}},
            {{-.14f, -.8f, -.12f}, {.14f, -.8f, -.12f}, {.14f, -.8f, .12f}, {-.14f, -.8f, .12f}}};
        for (int side = 0; side < 6; ++side) {
            float shade = side == 4 ? 1.f : side < 2 ? .85f : .7f;
            color({.68f * shade, .49f * shade, .36f * shade, 1});
            for (const auto& vertex : faces[side])
                glVertex3fv(vertex);
        }
        glEnd();
        return;
    }
    Color glint = hexColor(settings.get("glint", "#b56cff").asString());
    auto& texture = p->itemTexture(value, prestige, glint);
    GLuint face = texture.id;
    bool pet = value.get("id", "") == "pet";
    glRotatef(pet ? 0 : settings.get("tool_angle", 90).asFloat(), 0, 1, 0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, face);
    color({1, 1, 1, 1});
    if (!pet) {
        p->heldItem(texture);
        return;
    }
    auto plane = [&](float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1,
                     float v1) {
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0);
        glVertex3f(x0, y1, z);
        glTexCoord2f(u1, v0);
        glVertex3f(x1, y1, z);
        glTexCoord2f(u1, v1);
        glVertex3f(x1, y0, z);
        glTexCoord2f(u0, v1);
        glVertex3f(x0, y0, z);
        glEnd();
    };
    constexpr float d = .3f;
    plane(-.5f, -.5f, .5f, .5f, d, 0, 0, 1, 1);
    color({.78f, .78f, .78f, 1});
    plane(-.5f, -.5f, .5f, .5f, -d, 0, 0, 1, 1);
    glBindTexture(GL_TEXTURE_2D, p->itemTexture(value, prestige, glint, true).id);
    glBegin(GL_QUADS);
    for (int side = 0; side < 4; ++side) {
        color(side == 0 ? Color{1, 1, 1, 1} : Color{.65f, .65f, .65f, 1});
        for (int i = 0; i < 4; ++i) {
            float a = (i == 1 || i == 2) ? .5f : -.5f, z = i >= 2 ? d : -d;
            glTexCoord2f(i == 1 || i == 2 ? 1 : 0, i >= 2 ? 1 : 0);
            if (side < 2)
                glVertex3f(side == 0 ? .5f : -.5f, a, z);
            else
                glVertex3f(a, side == 2 ? .5f : -.5f, z);
        }
    }
    glEnd();
}
void Renderer::screenshot(const std::filesystem::path& path) {
    p->flushUi();
    std::vector<unsigned char> pixels(p->w * p->h * 4), flipped(pixels.size());
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, p->w, p->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < p->h; ++y)
        std::memcpy(&flipped[y * p->w * 4], &pixels[(p->h - y - 1) * p->w * 4], p->w * 4);
    png_image im{};
    im.version = PNG_IMAGE_VERSION;
    im.width = p->w;
    im.height = p->h;
    im.format = PNG_FORMAT_RGBA;
    auto file = imageFile(path, true);
    if (!file || !png_image_write_to_stdio(&im, file.get(), 0, flipped.data(), 0, nullptr))
        throw std::runtime_error("Screenshot failed");
}
const RenderStats& Renderer::stats() const {
    return p->stats;
}
} // namespace prison
