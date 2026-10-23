#include "prison/save.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif
namespace prison {
namespace {
constexpr std::size_t maxSaveBytes = 4 * 1024 * 1024;
std::int64_t now() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
void checkEnvelope(const Json& value) {
    if (!value.isObject() || !value["state"].isObject())
        throw std::runtime_error("Save has no world state.");
    if (!value.get("schema_version", 0).isIntegral() || value.get("schema_version", 0).asInt64() < 0 ||
        value.get("schema_version", 0).asInt64() > 3)
        throw std::runtime_error("This save uses a newer format.");
}
void atomicWrite(const std::filesystem::path& path, const std::string& data) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(data.data(), static_cast<std::streamsize>(data.size())) || !file.flush())
            throw std::runtime_error("Could not write save file.");
    }
#if defined(__unix__) || defined(__APPLE__)
    const int fd = ::open(temporary.c_str(), O_RDONLY);
    if (fd >= 0) {
        const int status = ::fsync(fd);
        ::close(fd);
        if (status != 0)
            throw std::runtime_error("Could not flush save file.");
    }
#endif
    if (std::filesystem::exists(path)) {
        try {
            checkEnvelope(readJson(path, maxSaveBytes));
            auto backup = path;
            backup += ".bak";
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception&) { /* A corrupt primary must not replace a good backup. */
        }
    }
    replaceFile(temporary, path);
#if defined(__unix__) || defined(__APPLE__)
    const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
#endif
}
} // namespace
SaveStore::SaveStore(std::filesystem::path directory) : directory_(std::move(directory)) {
    if (directory_.empty())
        throw std::runtime_error("Empty save directory.");
    std::filesystem::create_directories(directory_);
}
std::filesystem::path SaveStore::defaultDirectory() {
#ifdef _WIN32
    if (const char* base = std::getenv("LOCALAPPDATA"))
        if (*base)
            return std::filesystem::path(base) / "prisoncpp" / "saves";
#elif defined(__APPLE__)
    if (const char* base = std::getenv("HOME"))
        if (*base)
            return std::filesystem::path(base) / "Library/Application Support/prisoncpp/saves";
#endif
    if (const char* base = std::getenv("XDG_DATA_HOME"))
        if (*base)
            return std::filesystem::path(base) / "prisoncpp" / "saves";
    if (const char* base = std::getenv("HOME"))
        if (*base)
            return std::filesystem::path(base) / ".local/share/prisoncpp/saves";
    return std::filesystem::current_path() / "saves";
}
bool SaveStore::validId(const std::string& id) {
    if (id.empty() || id.size() > 64)
        return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-'))
            return false;
    return true;
}
Json SaveStore::load(const std::string& id) const {
    if (!validId(id))
        throw std::runtime_error("Invalid world ID.");
    const auto path = directory_ / (id + ".json");
    try {
        Json value = readJson(path, maxSaveBytes);
        checkEnvelope(value);
        return value;
    } catch (const std::exception&) {
        auto backup = path;
        backup += ".bak";
        Json value = readJson(backup, maxSaveBytes);
        checkEnvelope(value);
        return value;
    }
}
std::vector<Json> SaveStore::list() const {
    std::vector<Json> result;
    for (const auto& row : std::filesystem::directory_iterator(directory_)) {
        if (!row.is_regular_file() || row.path().extension() != ".json" ||
            !validId(row.path().stem().string()))
            continue;
        try {
            Json value = load(row.path().stem().string());
            Json slot = value.get("slot", Json(::Json::objectValue));
            if (!slot.isObject())
                slot = Json(::Json::objectValue);
            for (const char* key : {"last_saved_at", "created_at", "playtime_seconds"}) {
                if (!slot[key].isNumeric() || !std::isfinite(slot[key].asDouble()) ||
                    slot[key].asDouble() < 0 || slot[key].asDouble() > 1e15)
                    slot[key] = 0;
            }
            if (!slot["name"].isString())
                slot["name"] = "World";
            slot["id"] = row.path().stem().string();
            result.push_back(slot);
        } catch (const std::exception&) {
        }
    }
    std::sort(result.begin(), result.end(), [](const Json& a, const Json& b) {
        return a.get("last_saved_at", 0).asInt64() > b.get("last_saved_at", 0).asInt64();
    });
    return result;
}
Json SaveStore::save(const std::string& id, const std::string& name, const Json& snapshot) {
    if (!validId(id) || !snapshot.isObject())
        throw std::runtime_error("Invalid world save.");
    Json result(::Json::objectValue);
    if (std::filesystem::exists(directory_ / (id + ".json"))) {
        try {
            result = load(id);
        } catch (const std::exception&) {
        }
    }
    if (!result["slot"].isObject())
        result["slot"] = Json(::Json::objectValue);
    result["schema_version"] = 3;
    result["game_version"] = "cpp-0.1";
    result["slot"]["id"] = id;
    result["slot"]["name"] = name.substr(0, 128);
    if (!result["slot"].isMember("created_at"))
        result["slot"]["created_at"] = ::Json::Int64(now());
    result["slot"]["last_saved_at"] = ::Json::Int64(now());
    result["slot"]["playtime_seconds"] = snapshot.get("playtime_seconds", 0);
    result["state"] = snapshot;
    const auto text = jsonText(result);
    if (text.size() > maxSaveBytes)
        throw std::runtime_error("Save exceeds the size limit.");
    atomicWrite(directory_ / (id + ".json"), text);
    return result;
}
Json SaveStore::importFile(const std::filesystem::path& source, const std::string& name) {
    Json imported = readJson(source, maxSaveBytes);
    if (!imported.isObject())
        throw std::runtime_error("Save has no world state.");
    if (!imported.isMember("state")) {
        Json envelope(::Json::objectValue);
        envelope["state"] = imported;
        envelope["schema_version"] = 3;
        imported = envelope;
    }
    checkEnvelope(imported);
    if (!imported["state"]["money"].isNumeric())
        throw std::runtime_error("This file is not a world save.");
    if (!imported["state"].isMember("devmode"))
        imported["state"]["devmode"] = false;
    std::string id = "import-" + std::to_string(now());
    int suffix = 0;
    while (std::filesystem::exists(directory_ / (id + ".json")) ||
           std::filesystem::exists(directory_ / (id + ".json.bak")))
        id = "import-" + std::to_string(now()) + "-" + std::to_string(++suffix);
    if (!imported["slot"].isObject())
        imported["slot"] = Json(::Json::objectValue);
    imported["schema_version"] = 3;
    imported["game_version"] = "cpp-0.1";
    imported["slot"]["id"] = id;
    std::string title = name;
    if (title.empty() && imported["slot"]["name"].isString())
        title = imported["slot"]["name"].asString();
    if (title.empty())
        title = source.stem().u8string();
    imported["slot"]["name"] = title.substr(0, 128);
    imported["slot"]["playtime_seconds"] = imported["state"].get("playtime_seconds", 0);
    imported["slot"]["created_at"] = ::Json::Int64(now());
    imported["slot"]["last_saved_at"] = ::Json::Int64(now());
    auto text = jsonText(imported);
    if (text.size() > maxSaveBytes)
        throw std::runtime_error("Save exceeds the size limit.");
    atomicWrite(directory_ / (id + ".json"), text);
    return imported;
}
} // namespace prison
