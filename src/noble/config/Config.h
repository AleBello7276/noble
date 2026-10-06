#pragma once

#include "Settings.h"
#include <filesystem>
#include <string_view>
#include <vector>

namespace config {

struct LoadOptions {
    // an absent default file is allowed but an explicit path must exist
    std::filesystem::path file = "noble.toml";
    bool requireFile = false;
    std::filesystem::path titleFile;
    std::vector<std::string> overrides;
    bool readEnvironment = true;
};

struct LoadedSettings {
    Settings values;
    std::map<std::string, std::string> sources;
    // print effective toml values with descriptions and the source of each setting
    std::string Describe() const;
};

// apply preset defaults then explicit file, title, environment and command-line settings
LoadedSettings Load(const LoadOptions& options = {});

enum class Frontend { Normal, Debugger, Profile, TUI };

struct LaunchOptions {
    LoadedSettings config;
    std::string title;
    bool help = false;
    bool printConfig = false;
};

// share configuration arguments between frontends without exposing parser types
LaunchOptions ParseLaunch(int argc, char* argv[], Frontend frontend, std::string_view defaultTitle = {});
std::string Help(std::string_view program);

}  // namespace config
