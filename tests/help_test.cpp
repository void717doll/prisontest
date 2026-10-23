#include "prison/help.hpp"
#include <iostream>
#include <stdexcept>
using namespace prison;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        Data data;
        data.load(argc > 1 ? argv[1] : "..");
        const auto& commands = data.help["commands"];
        auto normalized = HelpUpdates::validate(data.help, commands);
        check(normalized.isObject(), "bundled help rejected");
        prison::Json hostile = data.help;
        prison::Json added;
        added["command"] = "remote-admin";
        added["description"] = "invented command";
        hostile["commands"].append(added);
        auto safe = HelpUpdates::validate(hostile, commands);
        check(safe["commands"].size() == commands.size(), "remote command injected into catalogue");
        hostile = data.help;
        hostile["commands"] = prison::Json(::Json::arrayValue);
        added = commands[0];
        added["description"] = "updated description";
        added["usage"] = prison::Json(::Json::objectValue);
        added["devmode"] = true;
        added["category"] = "Remote Category";
        hostile["commands"].append(added);
        safe = HelpUpdates::validate(hostile, commands);
        check(safe["commands"].size() == commands.size(), "partial help update removed local commands");
        check(safe["commands"][0]["description"] == "updated description" &&
                  safe["commands"][0]["usage"] == commands[0]["usage"] &&
                  safe["commands"][0]["category"] == commands[0]["category"] &&
                  safe["commands"][0]["devmode"] == commands[0]["devmode"],
              "remote help changed command syntax, category or developer visibility");
        hostile = data.help;
        hostile["sections"][0]["pages"][0]["body"] = std::string(8193, 'x');
        check(HelpUpdates::validate(hostile, commands).isNull(), "unbounded text accepted");
        hostile = data.help;
        hostile["sections"][0]["pages"][1]["id"] = hostile["sections"][0]["pages"][0]["id"];
        check(HelpUpdates::validate(hostile, commands).isNull(), "duplicate page accepted");
        hostile = data.help;
        hostile["sections"][0]["pages"][0]["body"] = prison::Json(::Json::objectValue);
        check(HelpUpdates::validate(hostile, commands).isNull(), "invalid body type accepted");
        hostile = data.help;
        hostile["commands"] = false;
        check(HelpUpdates::validate(hostile, commands).isNull(), "invalid commands type accepted");
        hostile = data.help;
        hostile["version"] = 2;
        check(HelpUpdates::validate(hostile, commands).isNull(), "unsupported version accepted");
        std::cout << "help validation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
