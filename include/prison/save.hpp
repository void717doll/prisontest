#pragma once
#include "common.hpp"
namespace prison {
// Isolated from Godot's save directory. Files are schema-3 envelopes.
class SaveStore {
  public:
    explicit SaveStore(std::filesystem::path directory);
    static std::filesystem::path defaultDirectory();
    const std::filesystem::path& directory() const {
        return directory_;
    }
    std::vector<Json> list() const;
    Json load(const std::string& id) const;
    Json save(const std::string& id, const std::string& name, const Json& snapshot);
    Json importFile(const std::filesystem::path& source, const std::string& name = "");
    static bool validId(const std::string& id);

  private:
    std::filesystem::path directory_;
};
} // namespace prison
