#pragma once

#include "Settings.h"
#include <filesystem>
#include <string_view>
#include <vector>

namespace config {

struct LoadOptions {
    // an empty path selects noble.toml beside the executable and creates documented defaults if missing
    // an explicitly supplied file must already exist
    std::filesystem::path file;
    std::filesystem::path titleFile;
    std::vector<std::string> overrides;
};

struct LoadedSettings {
    Settings values;
    std::map<std::string, std::string> sources;
    // print effective toml values with descriptions and the source of each setting
    std::string Describe() const;
};

// apply preset defaults then explicit file, title and command-line settings
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
