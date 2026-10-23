#include "prison/assets.hpp"
#include "prison/common.hpp"
#include "prison/render.hpp"
#include <cctype>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace prison {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char* endpoint = "https://assets.prisontest.com/";
constexpr const char* dnsFallbackUrl = "https://1.1.1.1/dns-query";
constexpr std::size_t maxFileBytes = 16 * 1024 * 1024;
constexpr std::size_t maxPackBytes = 128 * 1024 * 1024;

bool assetPath(const std::string& relative) {
    if (relative.empty() || relative.front() == '/' ||
        relative.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-") !=
            std::string::npos)
        return false;
    const std::filesystem::path path(relative);
    for (const auto& part : path)
        if (part == "." || part == "..")
            return false;
    const auto extension = path.extension();
    if (relative.rfind("data/", 0) == 0 && extension == ".json")
        return true;
    if (((relative.rfind("ui/", 0) == 0 || relative.rfind("textures/", 0) == 0) && extension == ".png") ||
        (relative.rfind("fonts/", 0) == 0 && extension == ".ttf") ||
        (relative.rfind("sounds/", 0) == 0 && (extension == ".wav" || extension == ".ogg")))
        return true;
    return false;
}

std::vector<std::string> assetFiles(const std::filesystem::path& bundle) {
    const auto manifest = bundle / "resources.txt";
    if (std::filesystem::file_size(manifest) > 65536)
        throw std::runtime_error("asset list is too large");
    std::ifstream input(manifest);
    if (!input)
        throw std::runtime_error("cannot read asset list");
    std::set<std::string> files;
    std::string relative;
    while (std::getline(input, relative)) {
        if (!relative.empty() && relative.back() == '\r')
            relative.pop_back();
        if (assetPath(relative))
            files.insert(relative);
    }
    if (input.bad() || files.empty())
        throw std::runtime_error("invalid asset list");
    return {files.begin(), files.end()};
}

std::string readBytes(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > maxFileBytes)
        throw std::runtime_error("asset is too large: " + path.filename().string());
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read asset: " + path.filename().string());
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
        throw std::runtime_error("cannot read asset: " + path.filename().string());
    return bytes;
}

void validateAssets(const std::filesystem::path& root, const std::vector<std::string>& files) {
    std::uintmax_t total = 0;
    for (const auto& relative : files) {
        const auto path = root / relative;
        const auto size = std::filesystem::file_size(path);
        total += size;
        if (!size || size > maxFileBytes || total > maxPackBytes)
            throw std::runtime_error("invalid asset size: " + relative);
        const auto extension = path.extension();
        if (extension == ".json") {
            const auto document = readJson(path);
            if (document.empty() ||
                (relative == "data/ranks.json" ? !document.isArray() : !document.isObject()))
                throw std::runtime_error("invalid data: " + relative);
        } else if (extension == ".png") {
            loadImage(path);
        } else if (extension == ".ttf") {
            const auto bytes = readBytes(path);
            FT_Library library = nullptr;
            if (FT_Init_FreeType(&library))
                throw std::runtime_error("cannot initialize font reader");
            FT_Face face = nullptr;
            const auto error = FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(bytes.data()),
                                                  FT_Long(bytes.size()), 0, &face);
            if (face)
                FT_Done_Face(face);
            FT_Done_FreeType(library);
            if (error)
                throw std::runtime_error("invalid font: " + relative);
        } else if (extension == ".wav") {
            SDL_AudioSpec spec{};
            Uint8* bytes = nullptr;
            Uint32 length = 0;
            if (!SDL_LoadWAV(path.u8string().c_str(), &spec, &bytes, &length))
                throw std::runtime_error("invalid sound: " + relative);
            SDL_FreeWAV(bytes);
        } else if (extension == ".ogg" && readBytes(path).compare(0, 4, "OggS") != 0) {
            throw std::runtime_error("invalid sound: " + relative);
        }
    }
    Data data;
    data.load(root);
}

bool validHeader(const Json& value) {
    if (!value.isString() || value.asString().empty() || value.asString().size() > 1024)
        return false;
    for (unsigned char c : value.asString())
        if (c < 32 || c == 127)
            return false;
    return true;
}

struct Download {
    CURL* request = nullptr;
    curl_slist* headers = nullptr;
    std::string body;
    Json validators;
    bool dnsFallback = false;
    explicit Download(const std::filesystem::path& bundle) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("cannot initialize asset downloads");
        request = curl_easy_init();
        if (!request) {
            curl_global_cleanup();
            throw std::runtime_error("cannot initialize asset request");
        }
        const auto certificates = bundle / "certs/ca-bundle.crt";
        if (std::filesystem::is_regular_file(certificates))
            curl_easy_setopt(request, CURLOPT_CAINFO, certificates.string().c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(request, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(request, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
        curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(request, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(request, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(request, CURLOPT_HEADERFUNCTION, receiveHeader);
        curl_easy_setopt(request, CURLOPT_HEADERDATA, this);
        curl_easy_setopt(request, CURLOPT_USERAGENT, "prisoncpp/0.1");
    }
    ~Download() {
        curl_easy_cleanup(request);
        curl_slist_free_all(headers);
        curl_global_cleanup();
    }
    Download(const Download&) = delete;
    Download& operator=(const Download&) = delete;
    static std::size_t receive(char* bytes, std::size_t size, std::size_t count, void* user) {
        auto& self = *static_cast<Download*>(user);
        if (count > maxFileBytes || size > maxFileBytes || size * count > maxFileBytes - self.body.size())
            return 0;
        self.body.append(bytes, size * count);
        return size * count;
    }
    static std::size_t receiveHeader(char* bytes, std::size_t size, std::size_t count, void* user) {
        if (size > 8192 || count > 8192 || size * count > 8192)
            return 0;
        auto& self = *static_cast<Download*>(user);
        std::string line(bytes, size * count);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (name == "etag" || name == "last-modified") {
                auto value = line.substr(colon + 1);
                const auto begin = value.find_first_not_of(" \t");
                const auto end = value.find_last_not_of(" \t\r\n");
                if (begin != std::string::npos && end != std::string::npos && end >= begin) {
                    value = value.substr(begin, end - begin + 1);
                    if (validHeader(value))
                        self.validators[name] = value;
                }
            }
        }
        return size * count;
    }
    long get(const std::string& relative, const Json& previous, long timeout) {
        body.clear();
        validators = Json(::Json::objectValue);
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, nullptr);
        curl_slist_free_all(headers);
        headers = nullptr;
        std::string condition;
        if (previous.isObject() && validHeader(previous["etag"]))
            condition = "If-None-Match: " + previous["etag"].asString();
        else if (previous.isObject() && validHeader(previous["last-modified"]))
            condition = "If-Modified-Since: " + previous["last-modified"].asString();
        if (!condition.empty()) {
            headers = curl_slist_append(nullptr, condition.c_str());
            if (!headers)
                throw std::runtime_error("cannot allocate asset request");
        }
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers);
        const std::string url = endpoint + relative;
        curl_easy_setopt(request, CURLOPT_URL, url.c_str());
        curl_easy_setopt(request, CURLOPT_TIMEOUT_MS, timeout);
        const auto started = Clock::now();
        auto result = curl_easy_perform(request);
        if (result == CURLE_COULDNT_RESOLVE_HOST && !dnsFallback) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
            const long remaining = timeout - long(elapsed);
            if (remaining > 0) {
                result = curl_easy_setopt(request, CURLOPT_DOH_URL, dnsFallbackUrl);
                if (result == CURLE_OK) {
                    std::cout << "assets: system dns failed; retrying with https dns\n";
                    dnsFallback = true;
                    // ignore any cached negative answer while switching resolvers.
                    curl_easy_setopt(request, CURLOPT_DNS_CACHE_TIMEOUT, 0L);
                    curl_easy_setopt(request, CURLOPT_TIMEOUT_MS, remaining);
                    body.clear();
                    validators = Json(::Json::objectValue);
                    result = curl_easy_perform(request);
                    curl_easy_setopt(request, CURLOPT_DNS_CACHE_TIMEOUT, 60L);
                }
            }
        }
        long status = 0;
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
        if (result != CURLE_OK)
            throw std::runtime_error(url + ": " + curl_easy_strerror(result));
        if (status != 200 && (status != 304 || condition.empty()))
            throw std::runtime_error(url + ": HTTP " + std::to_string(status));
        if (status == 304) {
            for (const auto& key : previous.getMemberNames())
                if (!validators.isMember(key) && validHeader(previous[key]))
                    validators[key] = previous[key];
        } else if (body.empty()) {
            throw std::runtime_error("empty asset: " + url);
        }
        return status;
    }
};

struct AssetPack {
    std::filesystem::path root;
    bool keep = false;
    explicit AssetPack(const std::filesystem::path& cache) {
        std::filesystem::create_directories(cache);
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            root = cache / ("pack-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(root))
                return;
        }
        throw std::runtime_error("cannot create asset cache");
    }
    ~AssetPack() {
        if (!keep) {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    }
    AssetPack(const AssetPack&) = delete;
    AssetPack& operator=(const AssetPack&) = delete;
};
} // namespace

std::filesystem::path updateAssets(const std::filesystem::path& bundle, const std::filesystem::path& cache) {
    std::filesystem::path selected;
    try {
        const auto files = assetFiles(bundle);
        Json previous;
        const auto index = cache / "current.json";
        try {
            if (std::filesystem::is_regular_file(index)) {
                previous = readJson(index, 256 * 1024);
                const auto name = previous.get("pack", "").asString();
                if (previous["endpoint"] != endpoint || !previous["files"].isObject() ||
                    name.rfind("pack-", 0) != 0 || name.size() <= 5 || name.size() > 80 ||
                    name.substr(5).find_first_not_of("0123456789-") != std::string::npos)
                    throw std::runtime_error("invalid asset cache index");
                validateAssets(cache / name, files);
                selected = cache / name;
            }
        } catch (const std::exception& error) {
            previous = Json();
            std::cerr << "assets: " << error.what() << '\n';
        }

        std::cout << "assets: checking " << endpoint << std::endl;
        Download download(bundle);
        AssetPack next(cache);
        bool changed = selected.empty();
        const auto deadline = Clock::now() + std::chrono::seconds(changed ? 60 : 20);
        std::size_t received = 0;
        Json metadata;
        metadata["endpoint"] = endpoint;
        for (const auto& relative : files) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0)
                throw std::runtime_error("asset update timed out");
            const auto status = download.get(relative, previous["files"][relative],
                                             long(std::min<std::int64_t>(remaining, 10000)));
            received += download.body.size();
            if (received > maxPackBytes)
                throw std::runtime_error("asset update is too large");
            const auto destination = next.root / relative;
            std::filesystem::create_directories(destination.parent_path());
            if (status == 304) {
                std::filesystem::copy_file(selected / relative, destination);
            } else {
                if (!changed && readBytes(selected / relative) != download.body)
                    changed = true;
                std::ofstream output(destination, std::ios::binary);
                output.write(download.body.data(), std::streamsize(download.body.size()));
                output.close();
                if (!output)
                    throw std::runtime_error("cannot cache asset: " + relative);
            }
            metadata["files"][relative] = download.validators;
        }
        validateAssets(next.root, files);
        metadata["pack"] = (changed ? next.root : selected).filename().string();
        const auto temporary = next.root / "index.tmp";
        {
            std::ofstream output(temporary);
            output << jsonText(metadata);
            output.close();
            if (!output)
                throw std::runtime_error("cannot save asset cache index");
        }
        replaceFile(temporary, index);
        if (changed) {
            // published packs stay immutable while other running clients may still use them.
            next.keep = true;
            selected = next.root;
        }
        std::cout << (changed ? "assets: downloaded and cached\n" : "assets: cache is current\n");
    } catch (const std::exception& error) {
        if (selected.empty())
            throw std::runtime_error(std::string("assets: ") + error.what() +
                                     "; a complete asset download is required before playing");
        std::cerr << "assets: " << error.what() << "; using cached files\n";
    }
    return selected;
}
} // namespace prison
