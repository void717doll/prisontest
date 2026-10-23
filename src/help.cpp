#include "prison/help.hpp"
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
namespace prison {
namespace {
constexpr std::size_t maxBytes = 131072;
bool validText(const Json& value, std::size_t limit) {
    if (!value.isString() || value.asString().size() > limit)
        return false;
    for (unsigned char c : value.asString())
        if (c < 32 && c != '\n' && c != '\r' && c != '\t')
            return false;
    return true;
}
bool validEndpoint(const std::string& endpoint) {
    if (endpoint.size() > 2048 || endpoint.find_first_of("@#\r\n\t ") != std::string::npos)
        return false;
    return endpoint.rfind("https://", 0) == 0 || endpoint.rfind("http://127.0.0.1:", 0) == 0;
}
} // namespace
Json HelpUpdates::validate(const Json& document, const Json& localCommands) {
    if (!document.isObject() || !document["version"].isNumeric() || document["version"].asDouble() != 1 ||
        !document["sections"].isArray() || document["sections"].size() > 32 ||
        !document["commands"].isArray() || document["commands"].size() > 256)
        return Json();
    Json result = document;
    std::set<std::string> ids;
    std::map<std::string, unsigned> commands;
    unsigned count = 0;
    for (auto& section : result["sections"]) {
        if (!section.isObject() || !validText(section["title"], 80) || !section["pages"].isArray())
            return Json();
        for (auto& page : section["pages"]) {
            if (++count > 256 || !page.isObject() || !validText(page["id"], 64) ||
                page["id"].asString().empty() || !ids.insert(page["id"].asString()).second ||
                !validText(page["title"], 100))
                return Json();
            if (page["body"].isString()) {
                Json lines(::Json::arrayValue);
                lines.append(page["body"]);
                page["body"] = lines;
            }
            if (!page["body"].isArray() || page["body"].empty() || page["body"].size() > 128)
                return Json();
            std::size_t bytes = 0;
            for (const auto& line : page["body"]) {
                if (!validText(line, 8192))
                    return Json();
                bytes += line.asString().size();
            }
            if (bytes > 8192)
                return Json();
            if (page.isMember("keywords")) {
                if (!page["keywords"].isArray() || page["keywords"].size() > 32)
                    return Json();
                for (const auto& word : page["keywords"])
                    if (!validText(word, 80))
                        return Json();
            }
        }
    }
    for (unsigned i = 0; i < localCommands.size(); ++i) {
        const auto& command = localCommands[i];
        if (command.isObject() && command["command"].isString())
            commands[command["command"].asString()] = i;
    }
    result["commands"] = localCommands;
    std::set<std::string> described;
    for (const auto& row : document["commands"]) {
        if (!row.isObject() || !validText(row["command"], 64) || !validText(row["description"], 2048))
            return Json();
        const auto name = row["command"].asString();
        if (!described.insert(name).second)
            return Json();
        auto local = commands.find(name);
        if (local != commands.end())
            result["commands"][local->second]["description"] = row["description"];
    }
    return count > 0 || !result["commands"].empty() ? result : Json();
}
struct HelpUpdates::Impl {
    Data& data;
    std::filesystem::path cache, certificates;
    std::string endpoint, body;
    Json localCommands;
    CURLM* multi = nullptr;
    CURL* request = nullptr;
    std::chrono::steady_clock::time_point refresh = std::chrono::steady_clock::now();
    Impl(Data& source, std::filesystem::path path, std::string url, std::filesystem::path ca)
        : data(source), cache(std::move(path)), certificates(std::move(ca)), endpoint(std::move(url)),
          localCommands(source.help["commands"]) {
        if (certificates.empty())
            certificates = data.root / "certs/ca-bundle.crt";
        if (endpoint.empty() || !validEndpoint(endpoint))
            return;
        try {
            if (std::filesystem::exists(cache)) {
                auto saved = readJson(cache, maxBytes + 4096);
                if (saved["endpoint"] == endpoint) {
                    auto valid = HelpUpdates::validate(saved["document"], localCommands);
                    if (!valid.isNull())
                        data.help = valid;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Help cache: " << e.what() << '\n';
        }
        if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
            multi = curl_multi_init();
    }
    ~Impl() {
        if (request) {
            curl_multi_remove_handle(multi, request);
            curl_easy_cleanup(request);
        }
        if (multi) {
            curl_multi_cleanup(multi);
            curl_global_cleanup();
        }
    }
    static std::size_t receive(char* bytes, std::size_t size, std::size_t count, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (count > maxBytes || size > maxBytes || size * count > maxBytes - self.body.size())
            return 0;
        self.body.append(bytes, size * count);
        return size * count;
    }
    void tick() {
        if (!multi)
            return;
        auto now = std::chrono::steady_clock::now();
        if (!request && now >= refresh) {
            refresh = now + std::chrono::minutes(15);
            body.clear();
            request = curl_easy_init();
            if (!request)
                return;
            curl_easy_setopt(request, CURLOPT_URL, endpoint.c_str());
            if (std::filesystem::is_regular_file(certificates))
                curl_easy_setopt(request, CURLOPT_CAINFO, certificates.string().c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
            curl_easy_setopt(request, CURLOPT_PROTOCOLS_STR, "http,https");
#else
            curl_easy_setopt(request, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
            curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(request, CURLOPT_TIMEOUT_MS, 10000L);
            curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
            curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, receive);
            curl_easy_setopt(request, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(request, CURLOPT_USERAGENT, "prisoncpp/0.1");
            curl_multi_add_handle(multi, request);
        }
        int running = 0;
        curl_multi_perform(multi, &running);
        int pending = 0;
        while (CURLMsg* message = curl_multi_info_read(multi, &pending)) {
            if (message->msg != CURLMSG_DONE)
                continue;
            long status = 0;
            curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
            if (message->data.result == CURLE_OK && status == 200) {
                ::Json::CharReaderBuilder reader;
                reader["stackLimit"] = 12;
                reader["rejectDupKeys"] = true;
                std::istringstream input(body);
                Json decoded;
                std::string error;
                if (::Json::parseFromStream(reader, input, &decoded, &error)) {
                    auto valid = HelpUpdates::validate(decoded, localCommands);
                    if (!valid.isNull()) {
                        data.help = valid;
                        try {
                            std::filesystem::create_directories(cache.parent_path());
                            Json saved;
                            saved["endpoint"] = endpoint;
                            saved["document"] = valid;
                            auto temp = cache;
                            temp += ".tmp";
                            {
                                std::ofstream out(temp);
                                out << jsonText(saved);
                                if (!out)
                                    throw std::runtime_error("Cannot save help cache");
                            }
                            replaceFile(temp, cache);
                        } catch (const std::exception& e) {
                            std::cerr << "Help cache: " << e.what() << '\n';
                        }
                    }
                }
            }
            curl_multi_remove_handle(multi, request);
            curl_easy_cleanup(request);
            request = nullptr;
        }
    }
};
HelpUpdates::HelpUpdates(Data& data, std::filesystem::path cache, std::string endpoint,
                         std::filesystem::path certificates)
    : p(std::make_unique<Impl>(data, std::move(cache), std::move(endpoint), std::move(certificates))) {}
HelpUpdates::~HelpUpdates() = default;
void HelpUpdates::tick() {
    p->tick();
}
} // namespace prison
