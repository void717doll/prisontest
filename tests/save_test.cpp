#include "prison/save.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Temp {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("prisoncpp-save-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() {
        std::filesystem::create_directories(path);
    }
    ~Temp() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file);
    out << text;
}
} // namespace
int main() {
    try {
        Temp temporary;
        prison::SaveStore store(temporary.path / "native");
        prison::Json state;
        state["money"] = 123;
        state["future_metadata"]["id"] = "preserved";
        state["playtime_seconds"] = 10;
        check(!prison::SaveStore::validId("../unsafe") && !prison::SaveStore::validId("") &&
                  prison::SaveStore::validId("world-1"),
              "world IDs bounded");
        store.save("world-1", "Test World", state);
        check(store.load("world-1")["state"] == state, "save roundtrip");
        state["money"] = 456;
        store.save("world-1", "Test World", state);
        write(store.directory() / "world-1.json", "{broken");
        check(store.load("world-1")["state"]["money"].asInt() == 123, "corrupt primary uses valid backup");
        store.save("world-1", "Recovered", state);
        check(store.load("world-1")["state"]["money"].asInt() == 456, "recovered world save");
        check(prison::readJson(store.directory() / "world-1.json.bak")["state"]["money"].asInt() == 123,
              "bad primary never replaces valid backup");
        prison::Json envelope;
        envelope["schema_version"] = 3;
        envelope["state"] = state;
        envelope["state"]["mine_state"]["seed"] = 42;
        envelope["future_envelope"] = "retained";
        envelope["slot"]["last_saved_at"] = "bad timestamp";
        envelope["slot"]["name"] = 123;
        const auto source = temporary.path / "godot.json";
        const auto original = prison::jsonText(envelope);
        write(source, original);
        auto imported = store.importFile(source, "Godot import");
        check(prison::jsonText(prison::readJson(source)) == original, "import leaves source untouched");
        check(!imported["state"]["devmode"].asBool(), "old import defaults devmode off");
        check(imported["state"]["mine_state"]["seed"].asInt() == 42, "import preserves mine");
        store.save(imported["slot"]["id"].asString(), "Imported", imported["state"]);
        check(store.load(imported["slot"]["id"].asString())["future_envelope"] == "retained",
              "future envelope roundtrip");
        write(store.directory() / "malformed.json", original);
        auto rows = store.list();
        check(rows.size() == 3, "malformed slot metadata cannot crash world listing");
        envelope["slot"]["name"] = "Original mine";
        write(source, prison::jsonText(envelope));
        auto named = store.importFile(source);
        check(named["slot"]["name"] == "Original mine", "import keeps the source world name");
        check(named["slot"]["playtime_seconds"] == state["playtime_seconds"], "import keeps listed playtime");
        auto again = store.importFile(source);
        check(again["slot"]["id"] != named["slot"]["id"], "repeated imports create separate copies");
        write(temporary.path / "old-world.json", prison::jsonText(state));
        check(store.importFile(temporary.path / "old-world.json")["slot"]["name"] == "old-world",
              "raw imports use the filename when no world name exists");
        envelope["schema_version"] = "three";
        write(temporary.path / "bad-version.json", prison::jsonText(envelope));
        bool rejected = false;
        try {
            store.importFile(temporary.path / "bad-version.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "schema type validation");
        envelope["schema_version"] = 4;
        write(temporary.path / "new-version.json", prison::jsonText(envelope));
        rejected = false;
        try {
            store.importFile(temporary.path / "new-version.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "future schema rejected");
        write(temporary.path / "oversized.json", std::string(4 * 1024 * 1024 + 1, ' '));
        rejected = false;
        try {
            store.importFile(temporary.path / "oversized.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "oversized import rejected");
        write(temporary.path / "not-a-world.json", "{\"currency_cap\":1000}");
        rejected = false;
        try {
            store.importFile(temporary.path / "not-a-world.json");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "gameplay tables must not be imported as worlds");
        std::cout << "save: atomic roundtrip, backup recovery, bounded import, isolated directories, unknown "
                     "metadata and invalid schema passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "save: " << error.what() << '\n';
        return 1;
    }
}
