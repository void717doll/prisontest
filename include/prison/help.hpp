#pragma once
#include "common.hpp"
#include <memory>
namespace prison {
class HelpUpdates {
  public:
    HelpUpdates(Data& data, std::filesystem::path cache, std::string endpoint,
                std::filesystem::path certificates = {});
    ~HelpUpdates();
    void tick();
    static Json validate(const Json& document, const Json& localCommands);

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace prison
