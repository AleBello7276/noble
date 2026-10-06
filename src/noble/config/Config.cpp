#include "Config.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <toml++/toml.hpp>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace config {

const char* Name(JITPreset value) {
    switch (value) {
    case JITPreset::Development:
        return "development";
    case JITPreset::Performance:
        return "performance";
    case JITPreset::Validation:
        return "validation";
    }
    throw std::invalid_argument("invalid jit preset");
}
const char* Name(Compilation value) {
    switch (value) {
    case Compilation::Blocks:
        return "blocks";
    case Compilation::Functions:
        return "functions";
    }
    throw std::invalid_argument("invalid compilation mode");
}
const char* Name(Optimization value) {
    switch (value) {
    case Optimization::None:
        return "none";
    case Optimization::Speed:
        return "speed";
    case Optimization::SpeedAndSize:
        return "speed_and_size";
    }
    throw std::invalid_argument("invalid optimization mode");
}
const char* Name(RegisterAllocation value) {
    switch (value) {
    case RegisterAllocation::Backtracking:
        return "backtracking";
    case RegisterAllocation::SinglePass:
        return "single_pass";
    }
    throw std::invalid_argument("invalid register allocation algorithm");
}

namespace {

std::string Quote(std::string_view value) {
    std::ostringstream result;
    result << toml::value(std::string(value));
    return result.str();
}

template <typename T>
T Read(const toml::node& node) {
    if constexpr (std::is_same_v<T, bool>) {
        if (const auto* value = node.as_boolean())
            return value->get();
        throw std::invalid_argument("expected a boolean");
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        if (const auto* value = node.as_integer(); value && value->get() >= 0 && value->get() <= UINT32_MAX)
            return static_cast<uint32_t>(value->get());
        throw std::invalid_argument("expected an unsigned 32-bit integer");
    } else {
        const auto* value = node.as_string();
        if (!value)
            throw std::invalid_argument("expected a string");
        const auto& name = value->get();
        if constexpr (std::is_same_v<T, std::string>) {
            return name;
        } else if constexpr (std::is_same_v<T, JITPreset>) {
            for (const auto option : {JITPreset::Development, JITPreset::Performance, JITPreset::Validation})
                if (name == Name(option))
                    return option;
            throw std::invalid_argument("expected development, performance or validation");
        } else if constexpr (std::is_same_v<T, Compilation>) {
            for (const auto option : {Compilation::Blocks, Compilation::Functions})
                if (name == Name(option))
                    return option;
            throw std::invalid_argument("expected blocks or functions");
        } else if constexpr (std::is_same_v<T, RegisterAllocation>) {
            for (const auto option : {RegisterAllocation::Backtracking, RegisterAllocation::SinglePass})
                if (name == Name(option))
                    return option;
            throw std::invalid_argument("expected backtracking or single_pass");
        } else {
            for (const auto option : {Optimization::None, Optimization::Speed, Optimization::SpeedAndSize})
                if (name == Name(option))
                    return option;
            throw std::invalid_argument("expected none, speed or speed_and_size");
        }
    }
}

struct Option {
    std::string_view key, description;
    std::function<void(Settings&, const toml::node&)> set;
    std::function<std::string(Settings)> get;
    bool inheritedDefault;
};

template <typename Accessor>
Option Bind(std::string_view key, std::string_view description, Accessor member,
            bool inheritedDefault = false, uint32_t minimum = 0, uint32_t maximum = UINT32_MAX) {
    using T = std::remove_reference_t<decltype(member(std::declval<Settings&>()))>;
    return {key, description,
            [member, minimum, maximum](Settings& settings, const toml::node& node) {
                const auto value = Read<T>(node);
                if constexpr (std::is_same_v<T, uint32_t>) {
                    if (value < minimum || value > maximum)
                        throw std::invalid_argument("expected an integer between " + std::to_string(minimum)
                                                    + " and " + std::to_string(maximum));
                }
                member(settings) = value;
            },
            [member](Settings settings) {
                const auto value = member(settings);
                if constexpr (std::is_same_v<T, bool>)
                    return std::string(value ? "true" : "false");
                else if constexpr (std::is_same_v<T, uint32_t>)
                    return std::to_string(value);
                else if constexpr (std::is_same_v<T, std::string>)
                    return Quote(value);
                else
                    return Quote(Name(value));
            },
            inheritedDefault};
}

// add a typed field and one binding here to expose a new setting to files and command-line overrides
const auto options = std::array{
    Bind("jit.preset", "base jit defaults: development, performance or validation",
         [](Settings& s) -> auto& { return s.jit.preset; }),
    Bind("jit.compilation", "compile bounded blocks or legacy function regions",
         [](Settings& s) -> auto& { return s.jit.compilation; }),
    Bind(
        "jit.branch_budget", "return to the dispatcher after this many native backward edges",
        [](Settings& s) -> auto& { return s.jit.branchBudget; }, false, 1, INT32_MAX),
    Bind("jit.dump_ir", "log generated cranelift ir before compilation",
         [](Settings& s) -> auto& { return s.jit.dumpIR; }),
    Bind(
        "jit.cranelift.opt_level", "none, speed or speed_and_size",
        [](Settings& s) -> auto& { return s.jit.optimization; }, true),
    Bind(
        "jit.cranelift.enable_verifier",
        "verify between compiler passes in addition to mandatory input verification",
        [](Settings& s) -> auto& { return s.jit.verifyPasses; }, true),
    Bind("jit.cranelift.regalloc_algorithm",
         "backtracking for code quality or single_pass for lower compilation latency with more spills and "
         "moves",
         [](Settings& s) -> auto& { return s.jit.registerAllocation; }),
    Bind("jit.cranelift.enable_alias_analysis",
         "remove redundant loads with speed or speed_and_size, has no effect with opt_level none",
         [](Settings& s) -> auto& { return s.jit.aliasAnalysis; }),
    Bind("jit.cranelift.preserve_frame_pointers",
         "retain native frame pointers for compatible sampling profilers and stack walkers",
         [](Settings& s) -> auto& { return s.jit.preserveFramePointers; }),
    Bind(
        "jit.cranelift.log2_min_function_alignment",
        "minimum alignment as a power of two from 0 to 12, zero keeps backend defaults, benchmark padding "
        "and code footprint",
        [](Settings& s) -> auto& { return s.jit.minFunctionAlignmentLog2; }, false, 0, 12),
    Bind("diagnostics.profiling", "collect process-wide worker phase timings",
         [](Settings& s) -> auto& { return s.diagnostics.profiling; }),
    Bind("diagnostics.execution_trace", "record block execution events when a trace sink is attached",
         [](Settings& s) -> auto& { return s.diagnostics.executionTrace; }),
    Bind("debugger.enabled", "attach instruction checkpoints before compiling guest code",
         [](Settings& s) -> auto& { return s.debugger.enabled; }),
    Bind(
        "debugger.break_on_entry", "pause newly scheduled threads until continued",
        [](Settings& s) -> auto& { return s.debugger.breakOnEntry; }, true)};

struct Layer {
    std::string source;
    toml::table table;
};

// resolve the running executable rather than relying on the working directory or argv[0]
std::filesystem::path DefaultPath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size)
            throw std::system_error(GetLastError(), std::system_category(), "unable to locate executable");
        if (size < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path() / "noble.toml";
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("unable to locate executable");
    return std::filesystem::canonical(buffer.data()).parent_path() / "noble.toml";
#elif defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe").parent_path() / "noble.toml";
#elif defined(__FreeBSD__)
    return std::filesystem::read_symlink("/proc/curproc/file").parent_path() / "noble.toml";
#else
#error executable path discovery needs an implementation for this platform
#endif
}

std::string PathName(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

// derive the initial file from the same definitions used to load and describe settings
std::string DefaultFile() {
    std::string result = "# noble session settings\n# edit this file before starting a new session\n\n";
    std::string section;
    for (const auto& option : options) {
        const auto split = option.key.rfind('.');
        const std::string next(option.key.substr(0, split));
        if (section != next) {
            section = next;
            result += "[" + section + "]\n\n";
        }
        const auto value = option.get(Settings{});
        result += "# " + std::string(option.description) + "\n# default: " + value + "\n";
        if (option.inheritedDefault) {
            result += "# leave commented to inherit the preset or frontend default\n# ";
        }
        result += std::string(option.key.substr(split + 1)) + " = " + value + "\n\n";
    }
    result += "[jit.cranelift.flags]\n# additional cranelift flags may be strings, booleans or integers\n"
              "# names and values are checked by cranelift during initialization\n"
              "# prefer the typed tuning options above when available\n"
              "# regalloc_checker = true\n";
    return result;
}

void CreateDefaults(const std::filesystem::path& path) {
    // exclusive creation preserves an existing file if another launch creates it first
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::noreplace);
    if (!file) {
        if (std::filesystem::exists(path))
            return;
        throw std::runtime_error("unable to create default configuration: " + PathName(path));
    }
    file << DefaultFile();
    file.close();
    if (!file)
        throw std::runtime_error("unable to write default configuration: " + PathName(path));
}

std::string Flag(const toml::node& node) {
    if (const auto* value = node.as_string())
        return value->get();
    if (const auto* value = node.as_boolean())
        return value->get() ? "true" : "false";
    if (const auto* value = node.as_integer())
        return std::to_string(value->get());
    throw std::invalid_argument("cranelift flags must be strings, booleans or integers");
}

void Apply(LoadedSettings& result, const toml::table& table, const std::string& source,
           const std::string& prefix = {}) {
    for (const auto& [key, node] : table) {
        const std::string path
            = prefix.empty() ? std::string(key.str()) : prefix + "." + std::string(key.str());
        const auto binding = std::find_if(options.begin(), options.end(),
                                          [&](const auto& option) { return option.key == path; });
        try {
            if (prefix == "jit.cranelift.flags") {
                if (key.str() == "opt_level" || key.str() == "enable_verifier")
                    throw std::invalid_argument("use the typed jit.cranelift setting for this flag");
                // normalize previously supported raw flags into their new typed settings
                const auto canonical = "jit.cranelift." + std::string(key.str());
                const auto typed = std::find_if(options.begin(), options.end(),
                                                [&](const auto& option) { return option.key == canonical; });
                if (typed != options.end()) {
                    const auto value = Flag(node);
                    const auto converted = toml::parse(
                        "value = " + (key.str() == "regalloc_algorithm" ? Quote(value) : value));
                    typed->set(result.values, *converted.get("value"));
                    result.sources[canonical] = source + " (flags table)";
                    continue;
                }
                result.values.jit.flags.insert_or_assign(std::string(key.str()), Flag(node));
            } else if (binding != options.end()) {
                binding->set(result.values, node);
            } else if (const auto* child = node.as_table()) {
                const bool known = path == "jit.cranelift.flags"
                                   || std::any_of(options.begin(), options.end(), [&](const auto& option) {
                                          return option.key.starts_with(path + ".");
                                      });
                if (!known)
                    throw std::invalid_argument("unknown configuration section");
                Apply(result, *child, source, path);
                continue;
            } else {
                throw std::invalid_argument("unknown configuration option");
            }
        } catch (const std::invalid_argument& error) {
            std::ostringstream message;
            message << source << ": " << path << ": " << error.what() << " (" << node.source() << ")";
            throw std::runtime_error(message.str());
        }
        result.sources[path] = source;
    }
}

}  // namespace

LoadedSettings Load(const LoadOptions& input) {
    std::vector<Layer> layers;
    const auto mainFile = input.file.empty() ? DefaultPath() : input.file;
    if (input.file.empty() && !std::filesystem::exists(mainFile))
        CreateDefaults(mainFile);
    auto file = [&](const std::filesystem::path& path) {
        if (path.empty())
            return;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("unable to read configuration: " + PathName(path));
        const std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if (stream.bad())
            throw std::runtime_error("unable to read configuration: " + PathName(path));
        layers.push_back({PathName(path), toml::parse(content, PathName(path))});
    };
    file(mainFile);
    file(input.titleFile);
    for (const auto& value : input.overrides)
        layers.push_back({"command line --set " + value, toml::parse(value)});

    LoadedSettings result;
    for (const auto& layer : layers) {
        if (const auto node = layer.table.at_path("jit.preset")) {
            try {
                result.values.jit.preset = Read<JITPreset>(*node.node());
            } catch (const std::exception& error) {
                throw std::runtime_error(layer.source + ": jit.preset: " + error.what());
            }
        }
    }
    if (result.values.jit.preset == JITPreset::Performance)
        result.values.jit.optimization = Optimization::Speed;
    if (result.values.jit.preset == JITPreset::Validation)
        result.values.jit.verifyPasses = true;
    for (const auto& option : options)
        result.sources[std::string(option.key)]
            = option.key.starts_with("jit.") ? std::string("preset ") + Name(result.values.jit.preset) :
                                               "default";
    for (const auto& layer : layers)
        Apply(result, layer.table, layer.source);
    return result;
}

std::string LoadedSettings::Describe() const {
    std::string result;
    for (const auto& option : options) {
        const auto source = sources.find(std::string(option.key));
        result += "# " + std::string(option.description)
                  + "\n# source: " + (source == sources.end() ? "default" : source->second) + "\n";
        result += std::string(option.key) + " = " + option.get(values) + "\n\n";
    }
    for (const auto& [key, value] : values.jit.flags) {
        const auto path = "jit.cranelift.flags." + key;
        const auto source = sources.find(path);
        result += "# source: " + (source == sources.end() ? "default" : source->second) + "\n";
        result += "jit.cranelift.flags." + Quote(key) + " = " + Quote(value) + "\n\n";
    }
    return result;
}

LaunchOptions ParseLaunch(int argc, char* argv[], Frontend frontend, std::string_view defaultTitle) {
    LaunchOptions result;
    result.title = defaultTitle;
    LoadOptions input;
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto value = [&]() -> std::string {
            if (++i >= argc)
                throw std::invalid_argument(std::string(arg) + " requires a value");
            if (!*argv[i])
                throw std::invalid_argument(std::string(arg) + " requires a nonempty value");
            return argv[i];
        };
        if (arg == "--help" || arg == "-h")
            result.help = true;
        else if (arg == "--print-config")
            result.printConfig = true;
        else if (arg == "--config") {
            input.file = value();
        } else if (arg == "--title-config")
            input.titleFile = value();
        else if (arg == "--set")
            input.overrides.push_back(value());
        else if (arg == "--debug")
            input.overrides.push_back("debugger.enabled = true");
        else if (arg == "--trace-execution")
            input.overrides.push_back("diagnostics.execution_trace = true");
        else if (arg.starts_with("-"))
            throw std::invalid_argument("unknown argument: " + std::string(arg));
        else if (positional)
            throw std::invalid_argument("only one title path is allowed");
        else {
            result.title = arg;
            positional = true;
        }
    }
    if (result.help)
        return result;
    result.config = Load(input);
    if (frontend == Frontend::Debugger) {
        result.config.values.debugger.enabled = true;
        result.config.sources["debugger.enabled"] = "debugger frontend";
    }
    if (frontend == Frontend::Profile) {
        result.config.values.diagnostics.profiling = true;
        result.config.sources["diagnostics.profiling"] = "profiling frontend";
        // profiling continues immediately unless the user explicitly requests entry pauses
        if (result.config.sources["debugger.break_on_entry"] == "default") {
            result.config.values.debugger.breakOnEntry = false;
            result.config.sources["debugger.break_on_entry"] = "profiling frontend";
        }
    }
    if (result.title.empty() && !result.printConfig)
        throw std::invalid_argument("a title path is required\n" + Help(argv[0]));
    return result;
}

std::string Help(std::string_view program) {
    return "usage: " + std::string(program)
           + " <title.xex> [options]\n"
             "  --config path        load a required file instead of noble.toml beside the executable\n"
             "  --title-config path  apply additional title settings after the main file\n"
             "  --set key=value      override a setting using a TOML value, repeatable\n"
             "  --debug              enable instruction debugger checkpoints\n"
             "  --trace-execution    record execution events when a trace sink is attached\n"
             "  --print-config       print effective settings and their sources without running\n"
             "  --help               show this help\n";
}

}  // namespace config
