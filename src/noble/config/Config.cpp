#include "Config.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <tuple>
#include <type_traits>

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
        if (const auto* value = node.as_integer(); value && value->get() > 0 && value->get() <= INT32_MAX)
            return static_cast<uint32_t>(value->get());
        throw std::invalid_argument("expected an integer between 1 and 2147483647");
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
};

template <typename Accessor>
Option Bind(std::string_view key, std::string_view description, Accessor member) {
    using T = std::remove_reference_t<decltype(member(std::declval<Settings&>()))>;
    return {key, description,
            [member](Settings& settings, const toml::node& node) { member(settings) = Read<T>(node); },
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
            }};
}

// add a typed field and one binding here to expose a new setting to files and command-line overrides
const auto options = std::array{
    Bind("jit.preset", "base jit defaults: development, performance or validation",
         [](Settings& s) -> auto& { return s.jit.preset; }),
    Bind("jit.compilation", "compile bounded blocks or legacy function regions",
         [](Settings& s) -> auto& { return s.jit.compilation; }),
    Bind("jit.branch_budget", "return to the dispatcher after this many native backward edges",
         [](Settings& s) -> auto& { return s.jit.branchBudget; }),
    Bind("jit.dump_ir", "log generated cranelift ir before compilation",
         [](Settings& s) -> auto& { return s.jit.dumpIR; }),
    Bind("jit.cranelift.opt_level", "none, speed or speed_and_size",
         [](Settings& s) -> auto& { return s.jit.optimization; }),
    Bind("jit.cranelift.enable_verifier",
         "verify between compiler passes in addition to mandatory input verification",
         [](Settings& s) -> auto& { return s.jit.verifyPasses; }),
    Bind("diagnostics.profiling", "collect process-wide worker phase timings",
         [](Settings& s) -> auto& { return s.diagnostics.profiling; }),
    Bind("diagnostics.execution_trace", "record block execution events when a trace sink is attached",
         [](Settings& s) -> auto& { return s.diagnostics.executionTrace; }),
    Bind("debugger.enabled", "attach instruction checkpoints before compiling guest code",
         [](Settings& s) -> auto& { return s.debugger.enabled; }),
    Bind("debugger.break_on_entry", "pause newly scheduled threads until continued",
         [](Settings& s) -> auto& { return s.debugger.breakOnEntry; })};

struct Layer {
    std::string source;
    toml::table table;
};

std::string Environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0)
        throw std::runtime_error("unable to read configuration environment");
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
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
    auto file = [&](const std::filesystem::path& path, bool required) {
        if (path.empty())
            return;
        if (!required && !std::filesystem::exists(path))
            return;
        layers.push_back({path.string(), toml::parse_file(path.string())});
    };
    file(input.file, input.requireFile);
    file(input.titleFile, !input.titleFile.empty());
    if (input.readEnvironment) {
        for (const auto& [name, key, quoted] :
             {std::tuple{"NOBLE_JIT_OPT_LEVEL", "jit.cranelift.opt_level", true},
              std::tuple{"NOBLE_JIT_COMPILATION", "jit.compilation", true},
              std::tuple{"NOBLE_JIT_VERIFY_PASSES", "jit.cranelift.enable_verifier", false}}) {
            const auto value = Environment(name);
            if (!value.empty())
                layers.push_back({std::string("environment ") + name,
                                  toml::parse(std::string(key) + " = " + (quoted ? Quote(value) : value))});
        }
    }
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
            return argv[i];
        };
        if (arg == "--help" || arg == "-h")
            result.help = true;
        else if (arg == "--print-config")
            result.printConfig = true;
        else if (arg == "--config") {
            input.file = value();
            input.requireFile = true;
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
             "  --config path        load a required configuration file instead of optional noble.toml\n"
             "  --title-config path  apply additional title settings after the main file\n"
             "  --set key=value      override a setting using a TOML value, repeatable\n"
             "  --debug              enable instruction debugger checkpoints\n"
             "  --trace-execution    record execution events when a trace sink is attached\n"
             "  --print-config       print effective settings and their sources without running\n"
             "  --help               show this help\n";
}

}  // namespace config
