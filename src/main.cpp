#include "prison/bombs.hpp"
#include "prison/game.hpp"
#include "prison/guardians.hpp"
#include "prison/help.hpp"
#include "prison/rewards.hpp"
#include "prison/save.hpp"
#include "prison/settings.hpp"
#include "prison/sound.hpp"
#include "prison/ui.hpp"
#include "prison/world.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <thread>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
namespace prison {
namespace {
using Clock = std::chrono::steady_clock;
void traceStartup(const char* phase) {
    if (std::getenv("PRISON_TRACE_STARTUP"))
        std::cerr << "startup: " << phase << '\n';
}
struct Options {
    std::filesystem::path assets, saveDir = SaveStore::defaultDirectory(), screenshot, import;
    std::vector<std::string> commands;
    std::string load, stress, menu;
    bool headless = false, newWorld = false, dev = false, noVsync = false;
    int frames = 0, iterations = 1000, width = 1280, height = 800;
};
std::filesystem::path discoverResources(const char* exe) {
    auto base = std::filesystem::absolute(exe).parent_path();
#ifdef __APPLE__
    // use dyld so command-line resource lookup does not initialize cocoa.
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> executablePath(size);
    if (_NSGetExecutablePath(executablePath.data(), &size) != 0)
        throw std::runtime_error("Cannot locate executable");
    base = std::filesystem::canonical(executablePath.data()).parent_path();
#else
    if (char* executableDirectory = SDL_GetBasePath()) {
        base = executableDirectory;
        SDL_free(executableDirectory);
    }
#endif
    std::vector<std::filesystem::path> paths = {base, base / "../Resources", base / "../share/prisoncpp"};
    for (const auto& p : paths)
        if (std::filesystem::is_regular_file(p / "resources.txt"))
            return std::filesystem::weakly_canonical(p);
    throw std::runtime_error("Cannot locate resources.txt; reinstall the client or use --assets PATH");
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() {
            if (++i >= argc)
                throw std::runtime_error("Missing value for " + a);
            return std::string(argv[i]);
        };
        if (a == "--assets")
            o.assets = value();
        else if (a == "--save-dir")
            o.saveDir = value();
        else if (a == "--load")
            o.load = value();
        else if (a == "--import")
            o.import = value();
        else if (a == "--command")
            o.commands.push_back(value());
        else if (a == "--headless")
            o.headless = true;
        else if (a == "--new")
            o.newWorld = true;
        else if (a == "--dev")
            o.dev = true;
        else if (a == "--no-vsync")
            o.noVsync = true;
        else if (a == "--frames")
            o.frames = std::clamp(std::stoi(value()), 1, 100000);
        else if (a == "--iterations")
            o.iterations = std::clamp(std::stoi(value()), 1, 1000000);
        else if (a == "--stress")
            o.stress = value();
        else if (a == "--screenshot")
            o.screenshot = value();
        else if (a == "--menu")
            o.menu = value();
        else if (a == "--width")
            o.width = std::clamp(std::stoi(value()), 640, 3840);
        else if (a == "--height")
            o.height = std::clamp(std::stoi(value()), 480, 2160);
        else if (a == "--help") {
            std::cout << "prisontest [--assets PATH] [--save-dir PATH] [--new --dev] [--load ID]\n  --import "
                         "FILE  --command TEXT  --headless  --stress mine|resetmine|rankup|prestige\n  "
                         "--iterations N  --frames N  --screenshot FILE  --menu PAGE  --no-vsync\n";
            std::exit(0);
        } else
            throw std::runtime_error("Unknown option: " + a);
    }
    if (o.assets.empty()) {
        traceStartup("locating client resources");
        o.assets = discoverResources(argv[0]);
    }
    o.saveDir = std::filesystem::absolute(o.saveDir);
    return o;
}
void finishReset(Mine& mine) {
    while (mine.resetting() || mine.pendingChunks())
        mine.step(2048, 4);
}
void startMine(Mine& mine, const Game& game) {
    mine.takePocketDrops(std::numeric_limits<int>::max());
    mine.configure(game.mineOptions());
    if (!mine.loadSnapshot(game.state["mine_state"], game.state["rank_index"].asInt(),
                           game.state["total_prestige"].asInt64()))
        mine.reset(game.state["rank_index"].asInt(), game.state["total_prestige"].asInt64());
}
struct SdlLifetime {
    SdlLifetime() {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
            throw std::runtime_error(SDL_GetError());
    }
    ~SdlLifetime() {
        SDL_Quit();
    }
};
struct WindowDeleter {
    void operator()(SDL_Window* p) const {
        SDL_DestroyWindow(p);
    }
};
Json capture(Game& game, Mine& mine, Vec3 position, float yaw, float pitch) {
    game.state["mine_state"] = mine.snapshot();
    Json p;
    p["x"] = position.x;
    p["y"] = position.y;
    p["z"] = position.z;
    p["yaw"] = yaw;
    p["pitch"] = pitch;
    game.state["native_player"] = p;
    return game.snapshot();
}
Json stress(Game& game, Mine& mine, const std::string& type, int iterations) {
    std::vector<double> times;
    times.reserve(iterations);
    finishReset(mine);
    auto begin = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto frame = Clock::now();
        if (type == "resetmine") {
            mine.reset(i % 26, game.state["total_prestige"].asInt64(), 1234 + i);
            finishReset(mine);
        } else if (type == "rankup") {
            game.state["rank_index"] = 0;
            game.state["rank_name"] = "A";
            game.state["money"] = 9e33;
            auto result = game.rankUp(true);
            if (!result.ok)
                throw std::runtime_error(result.message);
            mine.reset(game.state["rank_index"].asInt(), game.state["total_prestige"].asInt64());
            finishReset(mine);
        } else if (type == "prestige") {
            game.state["rank_index"] = 25;
            game.state["rank_name"] = "Z";
            game.state["prestige_level"] = 0;
            game.state["money"] = 9e33;
            auto result = game.prestige(true);
            if (!result.ok)
                throw std::runtime_error(result.message);
            mine.reset(0, game.state["total_prestige"].asInt64());
            finishReset(mine);
        } else if (type == "mine") {
            Cell cell;
            if (!mine.nextTarget(cell)) {
                mine.reset(game.state["rank_index"].asInt(), game.state["total_prestige"].asInt64());
                finishReset(mine);
                mine.nextTarget(cell);
            }
            auto block = mine.dig(cell);
            if (!block.empty())
                game.mineBlock(block, false, false);
            mine.step(2048, 2);
        } else
            throw std::runtime_error("Unknown stress type");
        times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - frame).count());
    }
    std::sort(times.begin(), times.end());
    Json result;
    result["type"] = type;
    result["iterations"] = iterations;
    result["elapsed_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    result["p50_ms"] = times[times.size() / 2];
    result["p95_ms"] = times[std::min(times.size() - 1, times.size() * 95 / 100)];
    result["max_ms"] = times.back();
    return result;
}
int run(const Options& options) {
    Data data;
    traceStartup("loading game data");
    data.load(options.assets);
    traceStartup("creating game");
    Game game(data);
    traceStartup("creating mine");
    Mine mine(data);
    Bombs bombs;
    RewardQueue procJobs;
    traceStartup("opening save directory");
    SaveStore saves(options.saveDir);
    std::string worldName = "Your Mine";
    std::string loadId = options.load;
    if (!options.import.empty()) {
        auto imported = saves.importFile(options.import);
        loadId = imported["slot"]["id"].asString();
        std::cout << "Imported world: " << loadId << '\n';
        if (options.headless)
            return 0;
    }
    if (!loadId.empty()) {
        auto envelope = saves.load(loadId);
        game.loadSnapshot(envelope["state"]);
        if (envelope["slot"]["name"].isString())
            worldName = envelope["slot"]["name"].asString();
    } else
        game.newWorld(options.dev);
    startMine(mine, game);
    if (!procJobs.restore(game.state["native_reward_jobs"], mine))
        throw std::runtime_error("Invalid pending mining rewards in save.");
    if (options.headless) {
        traceStartup("finishing mine reset");
        finishReset(mine);
        traceStartup("restoring mining rewards");
        while (!procJobs.empty())
            procJobs.step(game, mine);
        traceStartup("running headless commands");
        for (const auto& cmd : options.commands) {
            auto result = game.command(cmd);
            std::cout << (result.ok ? "OK " : "ERROR ") << result.message << '\n';
            if (!result.ok)
                return 2;
            if (result.resetMine) {
                mine.configure(game.mineOptions());
                mine.reset(game.state["rank_index"].asInt(), game.state["total_prestige"].asInt64());
                finishReset(mine);
            }
        }
        if (!options.stress.empty())
            std::cout << jsonText(stress(game, mine, options.stress, options.iterations)) << '\n';
        std::cout << "state blocks=" << game.state["blocks_mined"] << " rank=" << game.state["rank_name"]
                  << " pick=" << game.state["pick_level"] << '\n';
        return 0;
    }
    SdlLifetime sdl;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    std::unique_ptr<SDL_Window, WindowDeleter> window(
        SDL_CreateWindow("prisontest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options.width,
                         options.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!window)
        throw std::runtime_error(SDL_GetError());
    SDL_SetWindowMinimumSize(window.get(), 640, 480);
    auto icon = loadImage(data.root / "ui/prisontest_icon.png");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(icon.pixels.data(), icon.width, icon.height, 32,
                                                              icon.width * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        SDL_SetWindowIcon(window.get(), surface);
        SDL_FreeSurface(surface);
    }
    Renderer renderer(window.get(), data);
    if (options.noVsync)
        SDL_GL_SetSwapInterval(0);
    SoundManager audio(data.root);
    const char* helpEndpoint = std::getenv("PRISON_HELP_URL");
    HelpUpdates help(data, options.saveDir.parent_path() / "help-cache.json",
                     helpEndpoint ? helpEndpoint : "", options.assets / "certs/ca-bundle.crt");
    UiContext ctx;
    const auto settingsPath = options.saveDir.parent_path() / "settings.json";
    ctx.settings = loadSettings(settingsPath);
    if (ctx.settings.get("fullscreen", false).asBool())
        SDL_SetWindowFullscreen(window.get(), SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (options.noVsync)
        ctx.settings["vsync"] = false;
    SDL_GL_SetSwapInterval(ctx.settings.get("vsync", true).asBool() ? 1 : 0);
    ctx.buildHash = PRISON_BUILD_HASH;
    ctx.worlds = Json(::Json::arrayValue);
    for (const auto& w : saves.list())
        ctx.worlds.append(w);
    ctx.playing = options.newWorld || !loadId.empty();
    ctx.worldId = loadId;
    UI ui(game, renderer, ctx);
    if (ctx.playing)
        ui.open(options.menu);
    else
        ui.open("title");
    Vec3 position = mine.spawn();
    Vec3 velocity{};
    float yaw = 0, pitch = -.25f, vertical = 0;
    bool grounded = false, focused = true, running = true;
    float damage = 0, swing = 0, digSoundDelay = 0;
    Cell target{};
    bool held = false;
    double saveTime = 0;
    std::uint64_t generation = mine.generation();
    int guardianTarget = -1;
    std::future<Json> saving;
    auto restorePlayer = [&]() {
        position = mine.spawn();
        velocity = {};
        vertical = 0;
        yaw = 0;
        pitch = -.25f;
        const auto& p = game.state["native_player"];
        if (p.isObject() && p["x"].isNumeric() && p["y"].isNumeric() && p["z"].isNumeric()) {
            Vec3 saved{p["x"].asFloat(), p["y"].asFloat(), p["z"].asFloat()};
            const auto confined = mine.confinePlayer(saved);
            if (saved.x == confined.x && saved.y == confined.y && saved.z == confined.z &&
                saved.y < mine.maximum().y + 50) {
                position = saved;
                yaw = p["yaw"].isNumeric() && std::isfinite(p["yaw"].asDouble())
                          ? std::clamp(p["yaw"].asFloat(), -100.f, 100.f)
                          : 0;
                pitch = p["pitch"].isNumeric() && std::isfinite(p["pitch"].asDouble())
                            ? std::clamp(p["pitch"].asFloat(), -1.48f, 1.48f)
                            : -.25f;
            }
        }
    };
    restorePlayer();
    auto save = [&](bool async) {
        if (!ctx.playing)
            return;
        if (saving.valid()) {
            auto saved = saving.get();
            ctx.worldId = saved["slot"]["id"].asString();
        }
        game.state["native_reward_jobs"] = procJobs.snapshot(mine);
        Json snapshot = capture(game, mine, position, yaw, pitch);
        if (ctx.worldId.empty()) {
            const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            ctx.worldId = "world-" + std::to_string(stamp);
            while (std::filesystem::exists(saves.directory() / (ctx.worldId + ".json")))
                ctx.worldId += "-1";
        }
        std::string id = ctx.worldId, name = worldName;
        if (async)
            saving = std::async(std::launch::async,
                                [&saves, id, name, snapshot]() { return saves.save(id, name, snapshot); });
        else
            ctx.worldId = saves.save(id, name, snapshot)["slot"]["id"].asString();
        saveTime = 0;
    };
    auto reset = [&]() {
        game.state["guardian"]["active"] = false;
        mine.configure(game.mineOptions());
        mine.reset(game.state["rank_index"].asInt(), game.state["total_prestige"].asInt64());
        position = mine.spawn();
        yaw = mine.spawnYaw();
        pitch = -.25f;
        damage = 0;
        held = false;
        velocity = {};
        vertical = 0;
    };
    auto apply = [&](const Result& r) {
        ui.apply(r);
        if (r.ok)
            mine.configure(game.mineOptions());
        if (r.resetMine)
            reset();
    };
    for (const auto& command : options.commands)
        apply(game.command(command));
    if (ctx.playing && !options.stress.empty())
        apply(game.command("/test stress " + options.stress));
    auto summonGuardian = [&](bool development) {
        if (!game.spawnGuardian(mine.spawn() + Vec3{0, -.5f, -1.5f}, development))
            return false;
        for (const auto& part : guardianParts(game.state["guardian"]))
            if (position.x + .35f > part.low.x && position.x - .35f < part.high.x &&
                position.y + 1.8f > part.low.y && position.y < part.high.y &&
                position.z + .35f > part.low.z && position.z - .35f < part.high.z) {
                position = mine.spawn();
                velocity = {};
                vertical = 0;
                break;
            }
        return true;
    };
    auto reward = [&](const std::string& block, Cell origin, bool manual) {
        auto result = game.mineBlock(block, manual, manual);
        if (result.ok)
            game.recordShaftMining(1);
        for (const auto& proc : result.procs)
            procJobs.enqueue(game, mine, proc, origin, result.context);
        if (result.ok && manual && mine.resetRemaining() < 0 && summonGuardian(false))
            ctx.say("A Stone Guardian has awakened. Use /guardian to reach the mine entrance.");
    };
    Clock::time_point last = Clock::now();
    std::vector<double> frameTimes;
    if (options.frames)
        frameTimes.reserve(options.frames);
    int frame = 0;
    bool lastCapture = false;
    while (running) {
        help.tick();
        auto now = Clock::now();
        const double elapsed = std::max(0.0, std::chrono::duration<double>(now - last).count());
        const double dt = std::min(elapsed, .1);
        last = now;
        bool capture = ctx.playing && !ui.capturesInput() && focused;
        if (capture != lastCapture) {
            SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
            held = false;
            lastCapture = capture;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_DROPFILE) {
                if (!ctx.playing && event.drop.file &&
                    (ui.menu() == "title" || ui.menu() == "worlds" || ui.menu() == "import")) {
                    ctx.actions.push_back({"import", event.drop.file});
                    ui.open("import");
                }
                SDL_free(event.drop.file);
                continue;
            }
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                focused = false;
                held = false;
                vertical = 0;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                focused = true;
            bool gameInput = ctx.playing && !ui.capturesInput() && focused && !mine.resetting();
            if (gameInput && event.type == SDL_MOUSEMOTION) {
                const float sensitivity = .0025f * ctx.settings["mouse_sensitivity"].asFloat();
                yaw -= event.motion.xrel * sensitivity;
                pitch = std::clamp(pitch - event.motion.yrel * sensitivity, -1.48f, 1.48f);
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
                held = false;
            if (gameInput && event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    held = true;
                    int index = game.state.get("selected_hotbar_index", 0).asInt();
                    auto id = game.state["inventory_slots"][index].get("id", "").asString();
                    if (id != "pickaxe" && id != "minebomb")
                        apply(game.activateHeld(index, true));
                }
                int selected = game.state.get("selected_hotbar_index", 0).asInt();
                bool bombClick = event.button.button == SDL_BUTTON_LEFT &&
                                 game.state["inventory_slots"][selected].get("id", "") == "minebomb";
                if (event.button.button == SDL_BUTTON_RIGHT || bombClick) {
                    const Vec3 eye = position + Vec3{0, 1.62f, 0},
                               dir{-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                   -std::cos(yaw) * std::cos(pitch)};
                    int index = selected;
                    auto item = game.state["inventory_slots"][index];
                    if (item.get("id", "") == "minebomb") {
                        auto result = bombs.launch(mine, game, eye, dir, item.get("tier", 1).asInt());
                        if (result.ok) {
                            int count = item.get("count", 1).asInt() - 1;
                            if (count <= 0)
                                game.state["inventory_slots"][index] = Json();
                            else
                                game.state["inventory_slots"][index]["count"] = count;
                        } else
                            ui.apply(result);
                    } else
                        apply(game.activateHeld(index, false));
                }
            }
            if (gameInput && event.type == SDL_MOUSEWHEEL) {
                int slot = game.state.get("selected_hotbar_index", 0).asInt();
                game.state["selected_hotbar_index"] = (slot - event.wheel.y % 9 + 9) % 9;
            }
            if (gameInput && event.type == SDL_KEYDOWN && event.key.keysym.sym >= SDLK_1 &&
                event.key.keysym.sym <= SDLK_9)
                game.state["selected_hotbar_index"] = int(event.key.keysym.sym - SDLK_1);
            ui.event(event);
        }
        if (ui.capturesInput() || !focused) {
            held = false;
            damage = 0;
            velocity = {};
            vertical = 0;
        }
        auto actions = std::move(ctx.actions);
        ctx.actions.clear();
        for (const auto& action : actions) {
            try {
                if (action.kind == "quit") {
                    save(false);
                    ctx.actions.clear();
                    running = false;
                    break;
                } else if (action.kind == "new") {
                    save(false);
                    ctx.actions.clear();
                    procJobs.clear();
                    bombs.clear();
                    game.newWorld(ctx.settings.get("new_devmode", false).asBool() || action.value == "dev");
                    mine.takePocketDrops(std::numeric_limits<int>::max());
                    ctx.worldId = "";
                    worldName = action.value.empty() || action.value == "dev" ? "Your Mine" : action.value;
                    ctx.playing = true;
                    reset();
                    ui.open("");
                    break;
                } else if (action.kind == "load") {
                    save(false);
                    auto envelope = saves.load(action.value);
                    game.loadSnapshot(envelope["state"]);
                    ctx.actions.clear();
                    ctx.worldId = action.value;
                    worldName = envelope["slot"]["name"].isString() ? envelope["slot"]["name"].asString()
                                                                    : "Your Mine";
                    ctx.playing = true;
                    startMine(mine, game);
                    restorePlayer();
                    generation = mine.generation();
                    procJobs.clear();
                    bombs.clear();
                    if (!procJobs.restore(game.state["native_reward_jobs"], mine))
                        ui.apply({false, "Invalid pending mining rewards in save."});
                    ui.open("");
                    break;
                } else if (action.kind == "import" && !ctx.playing) {
                    std::string path = action.value;
                    if (path.size() >= 2 && ((path.front() == '"' && path.back() == '"') ||
                                             (path.front() == '\'' && path.back() == '\'')))
                        path = path.substr(1, path.size() - 2);
                    auto imported = saves.importFile(std::filesystem::u8path(path));
                    ctx.worlds = Json(::Json::arrayValue);
                    for (const auto& world : saves.list())
                        ctx.worlds.append(world);
                    ui.open("worlds");
                    ui.apply({true, "Imported " + imported["slot"]["name"].asString() + "."});
                    ctx.actions.clear();
                    break;
                } else if (action.kind == "title") {
                    save(false);
                    ctx.actions.clear();
                    ctx.playing = false;
                    ctx.worlds = Json(::Json::arrayValue);
                    for (const auto& w : saves.list())
                        ctx.worlds.append(w);
                    ui.open("title");
                    break;
                } else if (action.kind == "save")
                    save(false);
                else if (action.kind == "mine_options" && ctx.playing)
                    mine.configure(game.mineOptions());
                else if (action.kind == "shaft" && ctx.playing) {
                    if (!procJobs.empty()) {
                        const bool queued =
                            std::any_of(ctx.actions.begin(), ctx.actions.end(),
                                        [](const UiAction& item) { return item.kind == "shaft"; });
                        if (!queued)
                            ctx.actions.push_back(action);
                        ui.open("shaft");
                        continue;
                    }
                    if (mine.resetting()) {
                        ui.apply({false, "Wait for the mine to finish loading."});
                        continue;
                    }
                    Json args;
                    if (action.value == "start") {
                        args["mine"] = mine.snapshot();
                        args["player"]["x"] = position.x;
                        args["player"]["y"] = position.y;
                        args["player"]["z"] = position.z;
                        args["player"]["yaw"] = yaw;
                        args["player"]["pitch"] = pitch;
                    }
                    auto result = game.shaftAction(action.value, args);
                    if (result.ok) {
                        bombs.clear();
                        if (action.value == "leave") {
                            startMine(mine, game);
                            restorePlayer();
                            damage = 0;
                            held = false;
                        } else
                            reset();
                        generation = mine.generation();
                        result.resetMine = false;
                        ui.open("");
                    }
                    ui.apply(result);
                } else if (action.kind == "guardian" && ctx.playing) {
                    if (!mine.resetting() && summonGuardian(true)) {
                        ui.open("");
                        ctx.say("Stone Guardian summoned at the mine entrance.");
                    } else
                        ui.apply({false, "Guardian unavailable. Leave the shaft, stop automining or finish "
                                         "the current fight."});
                } else if (action.kind == "reset" && ctx.playing) {
                    if (game.inShaft())
                        ui.apply({false, "Use /shaft to descend or return to your mine."});
                    else
                        reset();
                } else if (action.kind == "mine_entry" && ctx.playing) {
                    position = mine.spawn();
                    yaw = mine.spawnYaw();
                    pitch = -.25f;
                    vertical = damage = 0;
                    velocity = {};
                    held = false;
                } else if (action.kind == "command" || action.kind == "progression") {
                    if (!ctx.playing)
                        continue;
                    std::string verb = action.value.substr(0, action.value.find(' '));
                    if (!verb.empty() && verb[0] == '/')
                        verb.erase(0, 1);
                    std::transform(verb.begin(), verb.end(), verb.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    bool progression = action.kind == "progression" || verb == "rankup" ||
                                       verb == "rankupmax" || verb == "prestige";
                    if (progression && !procJobs.empty()) {
                        bool queued =
                            std::any_of(ctx.actions.begin(), ctx.actions.end(), [](const UiAction& a) {
                                return a.kind == "command" || a.kind == "progression";
                            });
                        if (!queued)
                            ctx.actions.push_back(action);
                        ui.open("pick");
                    } else if (action.kind == "command")
                        apply(game.command(action.value));
                    else if (action.value == "rankup")
                        apply(game.rankUp());
                    else if (action.value == "rankupmax")
                        apply(game.rankUp(true));
                    else if (action.value == "prestige")
                        apply(game.prestige());
                    else if (action.value == "prestige_confirm")
                        apply(game.prestige(true));
                    else if (action.value == "rebirth_confirm")
                        apply(game.rebirth(true));
                } else if (action.kind == "stress" && game.state.get("devmode", false).asBool()) {
                    if (game.inShaft()) {
                        ui.apply({false, "Leave the shaft before starting a stress test."});
                        continue;
                    }
                    game.state["_stress"]["type"] = action.value;
                    game.state["_stress"]["active"] = true;
                    game.state["_stress"]["iterations"] = 0;
                } else if (action.kind == "fullscreen") {
                    if (SDL_SetWindowFullscreen(
                            window.get(), action.value == "on" ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
                        ui.apply({false, SDL_GetError()});
                } else if (action.kind == "settings") {
                    SDL_GL_SetSwapInterval(ctx.settings.get("vsync", true).asBool() ? 1 : 0);
                    saveSettings(settingsPath, ctx.settings);
                }
            } catch (const std::exception& e) {
                ui.apply({false, e.what()});
            }
        }
        if (!running)
            break;
        if (ctx.playing) {
            const bool shaftWasComplete = game.shaftComplete();
            procJobs.step(game, mine);
            mine.tick(dt);
            if (generation != mine.generation()) {
                game.state["guardian"]["active"] = false;
                generation = mine.generation();
                position = mine.spawn();
                yaw = 0;
                pitch = -.25f;
                vertical = 0;
                velocity = {};
                held = false;
            }
            mine.step();
            if (!game.inShaft() && game.state.get("devmode", false).asBool() &&
                game.state["_stress"].get("active", false).asBool() && !mine.resetting()) {
                const auto type = game.state["_stress"].get("type", "").asString();
                if (type == "mine") {
                    for (int i = 0; i < 8; ++i) {
                        Cell cell;
                        if (!mine.nextTarget(cell)) {
                            reset();
                            break;
                        }
                        auto block = mine.dig(cell);
                        if (!block.empty())
                            reward(block, cell, false);
                    }
                } else if (type == "resetmine") {
                    reset();
                } else if (type == "rankup") {
                    game.state["rank_index"] = 0;
                    game.state["rank_name"] = "A";
                    game.state["money"] = 9e33;
                    apply(game.rankUp(true));
                } else if (type == "prestige") {
                    game.state["rank_index"] = 25;
                    game.state["rank_name"] = "Z";
                    game.state["prestige_level"] = 0;
                    game.state["money"] = 9e33;
                    apply(game.prestige(true));
                }
                int count = game.state["_stress"].get("iterations", 0).asInt() + 1;
                game.state["_stress"]["iterations"] = count;
                if (count >= 1000) {
                    game.state["_stress"]["active"] = false;
                    ctx.say("Stress test finished: " + type + ", 1000 iterations.");
                }
            }
            ctx.resetting = mine.resetting();
            ctx.mineSize = {mine.width(), mine.height(), mine.depth()};
            bool input = !ui.capturesInput() && focused && !mine.resetting();
            if (input && game.tickGuardian(elapsed))
                ctx.say("The Stone Guardian returned to stone.");
            mine.setGuardian(game.state["guardian"]);
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (input) {
                float forward = (keys[SDL_SCANCODE_W] ? 1.f : 0) - (keys[SDL_SCANCODE_S] ? 1.f : 0),
                      side = (keys[SDL_SCANCODE_D] ? 1.f : 0) - (keys[SDL_SCANCODE_A] ? 1.f : 0);
                float norm = std::max(1.f, std::sqrt(forward * forward + side * side));
                int index = game.state.get("selected_hotbar_index", 0).asInt();
                bool pick = game.state["inventory_slots"][index].get("id", "") == "pickaxe";
                int speedLevel =
                    pick && game.state["message_settings"].get("speed_effect_enabled", true) != false
                        ? std::clamp(game.enchantLevel("speed"), 0, 5)
                        : 0;
                int jumpLevel =
                    pick && game.state["message_settings"].get("jump_effect_enabled", true) != false
                        ? std::clamp(game.enchantLevel("jump"), 0, 5)
                        : 0;
                float speed = 6.f * (1 + .12f * speedLevel);
                if (game.inShaft())
                    speed *= game.shaftCondition().get("speed_scale", 1).asFloat();
                Vec3 move{(-std::sin(yaw) * forward + std::cos(yaw) * side) * speed / norm, 0,
                          (-std::cos(yaw) * forward - std::sin(yaw) * side) * speed / norm};
                float acceleration = (grounded ? 18.f : 5.f) * float(dt);
                velocity.x += std::clamp(move.x - velocity.x, -acceleration, acceleration);
                velocity.z += std::clamp(move.z - velocity.z, -acceleration, acceleration);
                vertical = std::max(-30.f, vertical - float(dt) * 18);
                if (keys[SDL_SCANCODE_SPACE] && grounded)
                    vertical = 6.f * (1 + .12f * jumpLevel);
                velocity.y = vertical;
                auto motion = mine.movePlayer(position, velocity * float(dt));
                position = motion.position;
                grounded = motion.grounded;
                if (grounded)
                    vertical = 0;
                if (position.y < mine.minimum().y - 8) {
                    position = mine.spawn();
                    vertical = 0;
                    velocity = {};
                }
            }
            game.tick(elapsed);
            mine.applyVirtualMining(game.tickAutominer(elapsed, mine.rewardSample()), mine.generation());
            if (!mine.resetting()) {
                bombs.tick(mine, game, dt);
            }
            Vec3 eye = position + Vec3{0, 1.62f, 0},
                 dir{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
            auto hit = mine.raycast(eye, dir, 8);
            const auto guardianHit = raycastGuardian(game.state["guardian"], eye, dir,
                                                     hit.hit ? std::min(8.f, hit.distance) : 8.f);
            if (guardianHit.part != guardianTarget ||
                (guardianHit.part < 0 &&
                 (!hit.hit || target.x != hit.cell.x || target.y != hit.cell.y || target.z != hit.cell.z))) {
                damage = 0;
                target = hit.cell;
            }
            guardianTarget = guardianHit.part;
            int index = game.state.get("selected_hotbar_index", 0).asInt();
            bool pick = game.state["inventory_slots"][index].get("id", "") == "pickaxe";
            if (held && input && pick && guardianTarget >= 0 &&
                guardianTarget == guardianWeakPart(game.state["guardian"]) &&
                !game.state["autominer_active"].asBool()) {
                damage += dt / std::max(.2, game.data.systems["guardians"]["strike_seconds"].asDouble());
                swing = std::fmod(swing + float(dt) * 4, 1.f);
                if (damage >= 1) {
                    const auto result = game.strikeGuardian(guardianTarget);
                    if (!result.message.empty())
                        ui.apply(result);
                    if (result.ok)
                        audio.play(MiningSound::Break, ctx.settings.get("master_volume", 1).asFloat());
                    damage = 0;
                }
            } else if (held && input && guardianTarget < 0 && hit.mineable && pick && procJobs.size() < 61 &&
                       !game.state["autominer_active"].asBool() && !game.shaftComplete()) {
                digSoundDelay -= float(dt);
                if (digSoundDelay <= 0) {
                    audio.play(MiningSound::Dig, ctx.settings.get("master_volume", 1).asFloat());
                    digSoundDelay = .25f;
                }
                int efficiency = game.enchantLevel("efficiency");
                double power = 1 + std::clamp(game.enchantLevel("haste"), 0, 5) * .35 +
                               std::clamp(game.enchantLevel("speed"), 0, 5) * .20;
                if (efficiency > 0)
                    power += double(efficiency) * efficiency + 1;
                double duration = power <= 1 ? .7 : std::max(.005, .28 / power);
                if (game.inShaft())
                    duration = std::max(.08, duration) * game.state["shaft"]["floor"]["dig_scale"].asDouble();
                damage += dt / duration;
                swing = std::fmod(swing + float(dt) * 4, 1.f);
                if (damage >= 1) {
                    auto block = mine.dig(hit.cell);
                    if (!block.empty()) {
                        reward(block, hit.cell, true);
                        audio.play(MiningSound::Break, ctx.settings.get("master_volume", 1).asFloat());
                    }
                    damage = 0;
                }
            } else {
                damage = 0;
                digSoundDelay = 0;
                swing = std::max(0.f, swing - float(dt) * 4);
            }
            game.awardPocketDrops(mine.takePocketDrops());
            if (!shaftWasComplete && game.shaftComplete())
                ctx.say("Shaft floor cleared. Rewards received; /shaft to descend or return.");
            saveTime += dt;
            if (saveTime >= 120 && !saving.valid() && !mine.resetting())
                save(true);
            if (saving.valid() && saving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    ctx.worldId = saving.get()["slot"]["id"].asString();
                } catch (const std::exception& e) {
                    ui.apply({false, std::string("Save failed: ") + e.what()});
                }
            }
            if (game.state.isMember("mining_summary")) {
                ctx.say(miningSummaryText(game.data, game.state["mining_summary"]));
                game.state.removeMember("mining_summary");
            }
            float gui = std::clamp(ctx.settings.get("gui_scale", 1).asFloat(), .75f, 1.5f);
            int ww, wh;
            SDL_GetWindowSize(window.get(), &ww, &wh);
            gui = std::min(gui, std::min(ww / 760.f, wh / 600.f));
            renderer.begin(gui, mine.skyColor());
            renderer.world(mine, eye, yaw, pitch,
                           std::clamp(ctx.settings.get("fov", 72).asFloat(), 50.f, 120.f));
            if (input && guardianTarget < 0 && hit.mineable)
                renderer.selection(hit.cell, damage);
            renderer.guardian(game.state["guardian"], guardianTarget, damage);
            for (const auto& bomb : bombs.projectiles())
                renderer.billboard(bomb.position, .45f, "textures/minebomb.png", yaw, pitch);
            renderer.beginUI();
            renderer.mineHologram(mine, eye, yaw, pitch, ctx.settings.get("fov", 72).asFloat(),
                                  game.state["rank_index"].asInt(), game.state["shaft"]);
            Json heldItem = game.state["inventory_slots"][index];
            int tier = game.state.get("parts_prestige", 0).asInt();
            if (heldItem.get("id", "") == "pickaxe") {
                heldItem["enchanted"] = !game.state["enchants"].empty();
                for (const auto& part : game.state["equipped_parts"])
                    if (part.isObject())
                        tier = std::max(tier, part.get("tier", 1).asInt() - 1);
            }
            renderer.held(heldItem, tier, ctx.settings, swing);
        } else
            renderer.begin(ctx.settings.get("gui_scale", 1).asFloat());
        ctx.fps = dt > 0 ? ctx.fps * .9 + (1 / dt) * .1 : ctx.fps;
        renderer.beginUI();
        ui.draw();
        ++frame;
        bool end = options.frames && frame >= options.frames;
        if (end && !options.screenshot.empty())
            renderer.screenshot(options.screenshot);
        renderer.present();
        if (options.frames)
            frameTimes.push_back(std::chrono::duration<double, std::milli>(Clock::now() - now).count());
        if (end)
            running = false;
    }
    try {
        save(false);
    } catch (const std::exception& e) {
        std::cerr << "Save failed: " << e.what() << '\n';
        return 3;
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (!frameTimes.empty()) {
        std::sort(frameTimes.begin(), frameTimes.end());
        std::cout << "frames=" << frame << " p50_ms=" << frameTimes[frameTimes.size() / 2]
                  << " p95_ms=" << frameTimes[frameTimes.size() * 95 / 100] << " max_ms=" << frameTimes.back()
                  << " vertices=" << renderer.stats().terrainVertices
                  << " world_draws=" << renderer.stats().terrainDraws
                  << " ui_draws=" << renderer.stats().uiDraws << '\n';
    }
    return 0;
}
} // namespace
} // namespace prison
int main(int argc, char** argv) {
    try {
        prison::traceStartup("parsing arguments");
        const int status = prison::run(prison::parse(argc, argv));
        prison::traceStartup("exiting");
        return status;
    } catch (const std::exception& e) {
        std::cerr << "prisontest: " << e.what() << '\n';
        return 1;
    }
}
