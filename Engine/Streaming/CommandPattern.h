#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Rice::Streaming {

enum class CommandValueType { Int32, Int64, UInt32, UInt64, Float, Double, Bool, String };
using CommandValue = std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;
using NamedArguments = std::unordered_map<std::string, CommandValue>;

struct CommandContext {
    std::string instanceId;
    std::uint64_t generation = 0;
    std::string originSlotId;
    std::string mainSlotId;
    std::string liveChatId;
    std::string messageId;
    std::string authorChannelId; // verified transport metadata, never a pattern capture
};

struct CommandParameter {
    std::string name;
    CommandValueType type = CommandValueType::String;
    std::optional<CommandValue> defaultValue;
    std::function<CommandValue(const CommandContext&)> contextValue;
};
struct CommandFunctionDescriptor {
    std::string functionId;
    std::vector<CommandParameter> parameters;
};
struct CommandSymbol {
    CommandValueType type = CommandValueType::Int32;
    std::function<CommandValue()> read;
};
using CommandSymbols = std::unordered_map<std::string, CommandSymbol>;

struct PatternMatch {
    bool matched = false; // lexical pattern matched, even if argument conversion failed
    bool valid = false;
    NamedArguments arguments;
    std::string error;
};

class CommandPattern {
public:
    // Accepts a pattern or one Rice [YouTubeControllable : ...] annotation.
    // This parses metadata; it does not compile native C++ annotations.
    static CommandPattern Compile(std::string pattern,
        CommandFunctionDescriptor function, NamedArguments fixedValues = {},
        CommandSymbols symbols = {});
    PatternMatch Match(std::string_view input, const CommandContext& context = {}) const;
    bool Matches(std::string_view input) const; // lexical check; never reads symbols/context
    CommandPattern WithPattern(std::string pattern) const;
    const std::string& Text() const noexcept { return pattern_; }
    const std::string& CommandName() const noexcept { return commandName_; }
    const CommandFunctionDescriptor& Function() const noexcept { return function_; }

private:
    struct MappingValue {
        std::optional<CommandValue> literal;
        std::string symbol;
    };
    struct Token {
        std::string literal;
        std::string prefix;
        std::string suffix;
        std::string parameter;
        CommandValueType captureType = CommandValueType::String;
        std::unordered_map<std::string, MappingValue> mappings;
    };
    std::string pattern_;
    std::string commandName_;
    CommandFunctionDescriptor function_;
    NamedArguments fixedValues_;
    CommandSymbols symbols_;
    std::vector<Token> tokens_;
    bool Capture(std::string_view input, std::unordered_map<std::string, std::string>& values,
        std::string& error) const;
};

struct CommandBinding {
    std::string bindingId;
    CommandPattern pattern;
    std::function<void(const NamedArguments&, const CommandContext&)> invoke;
    std::function<bool(const CommandContext&)> authorized;
    std::function<bool()> available;
    bool enabled = true;
    std::string permission = "LocalAdmin";
    std::vector<std::string> allowedChannelIds;
};
enum class CommandDispatchStatus { Executed, Denied, Unavailable, InvalidArguments, Failed };
struct CommandDispatchItem {
    std::string bindingId;
    CommandDispatchStatus status;
    std::string detail;
};
struct CommandDispatchResult {
    std::vector<CommandDispatchItem> items;
    std::vector<std::string> diagnostics;
};

class CommandRegistry {
public:
    void Add(CommandBinding binding);
    bool Remove(std::string_view bindingId);
    void Clear() { bindings_.clear(); }
    std::size_t Count() const noexcept { return bindings_.size(); }
    CommandDispatchResult Dispatch(std::string_view input, const CommandContext& context = {}) const;
private:
    std::vector<CommandBinding> bindings_;
};

// Exposed to generated wrappers and inspector schema validation.
std::optional<CommandValue> ConvertCommandValue(const CommandValue& value, CommandValueType target);
std::optional<CommandValue> ParseCommandValue(std::string_view value, CommandValueType target);

} // namespace Rice::Streaming
