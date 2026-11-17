#include "CommandPattern.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Rice::Streaming {
namespace {
bool Space(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }
std::string_view Trim(std::string_view value) {
    while (!value.empty() && Space(value.front())) value.remove_prefix(1);
    while (!value.empty() && Space(value.back())) value.remove_suffix(1);
    return value;
}
std::string Unquote(std::string_view value) {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"')
        throw std::invalid_argument("Expected a quoted string");
    std::string result;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        char ch = value[index];
        if (ch == '"') throw std::invalid_argument("Unescaped quote");
        if (ch == '\\') {
            if (++index + 1 >= value.size()) throw std::invalid_argument("Incomplete string escape");
            ch = value[index];
            if (ch == 'n') ch = '\n';
            else if (ch == 't') ch = '\t';
            else if (ch == 'r') ch = '\r';
            else if (ch != '\\' && ch != '"') throw std::invalid_argument("Unsupported string escape");
        }
        result.push_back(ch);
    }
    return result;
}
std::string Atom(std::string_view value) {
    value = Trim(value);
    if (value.empty()) throw std::invalid_argument("Empty pattern value");
    return value.front() == '"' ? Unquote(value) : std::string(value);
}
bool Identifier(std::string_view value) {
    if (value.empty()) return false;
    auto letter = [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch >= 128;
    };
    if (!letter(static_cast<unsigned char>(value.front()))) return false;
    return std::all_of(value.begin() + 1, value.end(), [&](unsigned char ch) {
        return letter(ch) || (ch >= '0' && ch <= '9');
    });
}
std::vector<std::string> InputTokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::string token;
    bool quoted = false, escaped = false, present = false;
    for (char ch : text) {
        if (escaped) {
            if (ch != '"' && ch != '\\') throw std::invalid_argument("Invalid input escape");
            token.push_back(ch); escaped = false; present = true;
        } else if (ch == '\\' && quoted) escaped = true;
        else if (ch == '"') { quoted = !quoted; present = true; }
        else if (Space(ch) && !quoted) {
            if (present) { tokens.push_back(std::move(token)); token.clear(); present = false; }
        } else { token.push_back(ch); present = true; }
    }
    if (quoted || escaped) throw std::invalid_argument("Unterminated input string");
    if (present) tokens.push_back(std::move(token));
    return tokens;
}
std::vector<std::string> PatternTokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::string token;
    int braces = 0;
    bool quoted = false, escaped = false;
    for (char ch : text) {
        if (!quoted && ch == '{' && ++braces > 1) throw std::invalid_argument("Nested capture");
        if (!quoted && ch == '}' && --braces < 0) throw std::invalid_argument("Unexpected closing brace");
        if (Space(ch) && !quoted && braces == 0) {
            if (!token.empty()) { tokens.push_back(std::move(token)); token.clear(); }
            continue;
        }
        token.push_back(ch);
        if (escaped) escaped = false;
        else if (quoted && ch == '\\') escaped = true;
        else if (ch == '"') quoted = !quoted;
    }
    if (braces || quoted || escaped) throw std::invalid_argument("Unterminated pattern capture or quote");
    if (!token.empty()) tokens.push_back(std::move(token));
    return tokens;
}
CommandValueType Type(std::string_view name) {
    if (name == "int" || name == "int32_t" || name == "std::int32_t") return CommandValueType::Int32;
    if (name == "int64_t" || name == "std::int64_t") return CommandValueType::Int64;
    if (name == "unsigned" || name == "unsigned int" || name == "uint32_t" || name == "std::uint32_t") return CommandValueType::UInt32;
    if (name == "uint64_t" || name == "std::uint64_t") return CommandValueType::UInt64;
    if (name == "float") return CommandValueType::Float;
    if (name == "double") return CommandValueType::Double;
    if (name == "bool") return CommandValueType::Bool;
    if (name == "string" || name == "std::string") return CommandValueType::String;
    throw std::invalid_argument("Unsupported command parameter type: " + std::string(name));
}
bool Convertible(CommandValueType from, CommandValueType to) {
    if (from == CommandValueType::String || to == CommandValueType::String) return from == to;
    // bool is kept explicit: a user-supplied numeric value must not silently
    // become authorization or an enable flag through truthiness.
    if (from == CommandValueType::Bool || to == CommandValueType::Bool) return from == to;
    return true;
}
std::optional<CommandValue> Literal(std::string_view text) {
    text = Trim(text);
    if (text.empty()) return std::nullopt;
    if (text.front() == '"') return CommandValue(Unquote(text));
    if (text == "true") return CommandValue(true);
    if (text == "false") return CommandValue(false);
    if (auto value = ParseCommandValue(text, CommandValueType::Int64)) return value;
    if (auto value = ParseCommandValue(text, CommandValueType::UInt64)) return value;
    return ParseCommandValue(text, CommandValueType::Double);
}
// Find a delimiter outside quoted text, including escaped quotes.
std::size_t FindOutside(std::string_view text, char delimiter) {
    bool quoted = false, escaped = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (escaped) escaped = false;
        else if (quoted && ch == '\\') escaped = true;
        else if (ch == '"') quoted = !quoted;
        else if (!quoted && ch == delimiter) return index;
    }
    return std::string_view::npos;
}
const CommandParameter& Parameter(const CommandFunctionDescriptor& function, std::string_view name) {
    auto found = std::find_if(function.parameters.begin(), function.parameters.end(), [&](const auto& item) {
        return item.name == name;
    });
    if (found == function.parameters.end()) throw std::invalid_argument("Unknown function parameter: " + std::string(name));
    return *found;
}
}

std::optional<CommandValue> ConvertCommandValue(const CommandValue& value, CommandValueType target) {
    if (target == CommandValueType::String) {
        if (auto text = std::get_if<std::string>(&value)) return *text;
        return std::nullopt;
    }
    if (target == CommandValueType::Bool) {
        if (auto boolean = std::get_if<bool>(&value)) return *boolean;
        return std::nullopt;
    }
    if (std::holds_alternative<std::string>(value) || std::holds_alternative<bool>(value)) return std::nullopt;
    if (target == CommandValueType::Float || target == CommandValueType::Double) {
        double result = 0.0;
        if (auto signedValue = std::get_if<std::int64_t>(&value)) {
            result = static_cast<double>(*signedValue);
            // Avoid out-of-range back casts at INT64_MAX/UINT64_MAX.
            if (result >= 9223372036854775808.0 || result < -9223372036854775808.0 ||
                static_cast<std::int64_t>(result) != *signedValue) return std::nullopt;
        } else if (auto unsignedValue = std::get_if<std::uint64_t>(&value)) {
            result = static_cast<double>(*unsignedValue);
            if (result >= 18446744073709551616.0 || static_cast<std::uint64_t>(result) != *unsignedValue)
                return std::nullopt;
        } else result = std::get<double>(value);
        if (!std::isfinite(result)) return std::nullopt;
        if (target == CommandValueType::Float) {
            if (std::abs(result) > std::numeric_limits<float>::max()) return std::nullopt;
            const float narrowed = static_cast<float>(result);
            if (static_cast<double>(narrowed) != result) return std::nullopt;
        }
        return result;
    }
    if (target == CommandValueType::Int32 || target == CommandValueType::Int64) {
        std::int64_t result = 0;
        if (auto signedValue = std::get_if<std::int64_t>(&value)) result = *signedValue;
        else if (auto unsignedValue = std::get_if<std::uint64_t>(&value)) {
            if (*unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
            result = static_cast<std::int64_t>(*unsignedValue);
        } else {
            const double real = std::get<double>(value);
            if (!std::isfinite(real) || std::trunc(real) != real || real < -9223372036854775808.0 || real >= 9223372036854775808.0) return std::nullopt;
            result = static_cast<std::int64_t>(real);
        }
        if (target == CommandValueType::Int32 &&
            (result < std::numeric_limits<std::int32_t>::min() || result > std::numeric_limits<std::int32_t>::max())) return std::nullopt;
        return result;
    }
    std::uint64_t result = 0;
    if (auto unsignedValue = std::get_if<std::uint64_t>(&value)) result = *unsignedValue;
    else if (auto signedValue = std::get_if<std::int64_t>(&value)) {
        if (*signedValue < 0) return std::nullopt;
        result = static_cast<std::uint64_t>(*signedValue);
    } else {
        const double real = std::get<double>(value);
        if (!std::isfinite(real) || std::trunc(real) != real || real < 0.0 || real >= 18446744073709551616.0) return std::nullopt;
        result = static_cast<std::uint64_t>(real);
    }
    if (target == CommandValueType::UInt32 && result > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return result;
}

std::optional<CommandValue> ParseCommandValue(std::string_view text, CommandValueType target) {
    if (target == CommandValueType::String) return std::string(text);
    if (target == CommandValueType::Bool) {
        if (text == "true") return true;
        if (text == "false") return false;
        return std::nullopt;
    }
    if (text.empty()) return std::nullopt;
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) return std::nullopt;
    }
    if (text.empty()) return std::nullopt;
    const char* end = text.data() + text.size();
    if (target == CommandValueType::Float) {
        float number = 0;
        auto result = std::from_chars(text.data(), end, number);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(number)) return std::nullopt;
        return static_cast<double>(number);
    }
    if (target == CommandValueType::Double) {
        double number = 0;
        auto result = std::from_chars(text.data(), end, number);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(number)) return std::nullopt;
        return number;
    }
    if (target == CommandValueType::Int32 || target == CommandValueType::Int64) {
        std::int64_t number = 0;
        auto result = std::from_chars(text.data(), end, number);
        if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
        return ConvertCommandValue(number, target);
    }
    std::uint64_t number = 0;
    auto result = std::from_chars(text.data(), end, number);
    if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
    return ConvertCommandValue(number, target);
}

CommandPattern CommandPattern::Compile(std::string pattern, CommandFunctionDescriptor function,
    NamedArguments fixedValues, CommandSymbols symbols) {
    if (function.functionId.empty()) throw std::invalid_argument("FunctionId is required");
    CommandPattern result;
    result.function_ = std::move(function);
    result.fixedValues_ = std::move(fixedValues);
    result.symbols_ = std::move(symbols);
    std::unordered_set<std::string> parameterNames;
    for (auto& parameter : result.function_.parameters) {
        if (!Identifier(parameter.name) || !parameterNames.insert(parameter.name).second)
            throw std::invalid_argument("Invalid or duplicate parameter name");
        if (parameter.defaultValue) {
            parameter.defaultValue = ConvertCommandValue(*parameter.defaultValue, parameter.type);
            if (!parameter.defaultValue) throw std::invalid_argument("Default argument type mismatch: " + parameter.name);
        }
    }
    for (auto& [name, value] : result.fixedValues_) {
        const auto& parameter = Parameter(result.function_, name);
        auto converted = ConvertCommandValue(value, parameter.type);
        if (!converted) throw std::invalid_argument("Fixed argument type mismatch: " + name);
        value = std::move(*converted);
    }
    auto text = Trim(pattern);
    if (text.starts_with("[YouTubeControllable")) {
        constexpr std::string_view prefix = "[YouTubeControllable";
        text.remove_prefix(prefix.size());
        text = Trim(text);
        if (text.empty() || text.front() != ':' || text.back() != ']')
            throw std::invalid_argument("Malformed YouTubeControllable annotation");
        text.remove_prefix(1); text.remove_suffix(1); text = Trim(text);
    }
    result.pattern_ = !text.empty() && text.front() == '"' ? Unquote(text) : std::string(text);
    auto tokens = PatternTokens(result.pattern_);
    if (tokens.empty()) throw std::invalid_argument("Empty command pattern");
    std::unordered_set<std::string> captured;
    for (const auto& raw : tokens) {
        Token token;
        const auto open = raw.find('{');
        if (open == std::string::npos) {
            token.literal = Atom(raw);
            result.tokens_.push_back(std::move(token));
            continue;
        }
        const auto close = raw.rfind('}');
        if (close == std::string::npos || raw.find('{', open + 1) != std::string::npos ||
            raw.find('}', open + 1) != close) throw std::invalid_argument("Only one capture per token is supported");
        token.prefix = raw.substr(0, open);
        token.suffix = raw.substr(close + 1);
        std::string_view body(raw.data() + open + 1, close - open - 1);
        // The mapping ':' must not be confused with std:: in a type name.
        const auto firstPair = FindOutside(body, '(');
        auto header = Trim(firstPair == std::string_view::npos ? body : body.substr(0, firstPair));
        if (!header.empty() && header.back() == ':') header = Trim(header.substr(0, header.size() - 1));
        const auto split = header.find_last_of(" \t\r\n");
        token.parameter = std::string(split == std::string_view::npos ? header : Trim(header.substr(split + 1)));
        const auto& parameter = Parameter(result.function_, token.parameter);
        token.captureType = split == std::string_view::npos ? parameter.type : Type(Trim(header.substr(0, split)));
        if (!Convertible(token.captureType, parameter.type)) throw std::invalid_argument("Capture type is not convertible: " + token.parameter);
        if (!captured.insert(token.parameter).second || result.fixedValues_.contains(token.parameter) || parameter.contextValue)
            throw std::invalid_argument("Parameter has multiple value sources: " + token.parameter);
        if (firstPair != std::string_view::npos) {
            auto pairs = Trim(body.substr(firstPair));
            while (!pairs.empty()) {
                if (pairs.front() != '(') throw std::invalid_argument("Expected mapping pair");
                pairs.remove_prefix(1);
                const auto pairEnd = FindOutside(pairs, ')');
                if (pairEnd == std::string_view::npos) throw std::invalid_argument("Unterminated mapping pair");
                auto pair = pairs.substr(0, pairEnd);
                const auto comma = FindOutside(pair, ',');
                if (comma == std::string_view::npos || FindOutside(pair.substr(comma + 1), ',') != std::string_view::npos)
                    throw std::invalid_argument("Mapping pair requires one key and one value");
                const auto key = Atom(pair.substr(0, comma));
                const auto expression = Trim(pair.substr(comma + 1));
                MappingValue mapped;
                mapped.literal = Literal(expression);
                if (mapped.literal) {
                    mapped.literal = ConvertCommandValue(*mapped.literal, token.captureType);
                    if (!mapped.literal || !ConvertCommandValue(*mapped.literal, parameter.type))
                        throw std::invalid_argument("Mapping literal type mismatch: " + token.parameter);
                } else {
                    mapped.symbol = std::string(expression);
                    auto symbol = result.symbols_.find(mapped.symbol);
                    if (symbol == result.symbols_.end() || !symbol->second.read ||
                        !Convertible(symbol->second.type, token.captureType))
                        throw std::invalid_argument("Unknown or incompatible mapping symbol: " + mapped.symbol);
                }
                if (!token.mappings.emplace(key, std::move(mapped)).second)
                    throw std::invalid_argument("Duplicate mapping key: " + key);
                pairs = Trim(pairs.substr(pairEnd + 1));
                if (!pairs.empty()) {
                    if (pairs.front() != ',') throw std::invalid_argument("Expected mapping separator");
                    pairs = Trim(pairs.substr(1));
                    if (pairs.empty()) throw std::invalid_argument("Trailing mapping separator");
                }
            }
        }
        result.tokens_.push_back(std::move(token));
    }
    for (const auto& parameter : result.function_.parameters)
        if (!captured.contains(parameter.name) && !result.fixedValues_.contains(parameter.name) &&
            !parameter.defaultValue && !parameter.contextValue)
            throw std::invalid_argument("Missing required parameter: " + parameter.name);
    if (result.tokens_.front().literal.empty()) throw std::invalid_argument("Command must start with a literal token");
    result.commandName_ = result.tokens_.front().literal;
    return result;
}

bool CommandPattern::Capture(std::string_view input,
    std::unordered_map<std::string, std::string>& captures, std::string& error) const {
    std::vector<std::string> values;
    try { values = InputTokens(input); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (values.size() != tokens_.size()) return false;
    for (std::size_t index = 0; index < tokens_.size(); ++index) {
        const auto& token = tokens_[index];
        const auto& value = values[index];
        if (token.parameter.empty()) { if (token.literal != value) return false; continue; }
        if (!value.starts_with(token.prefix) || !value.ends_with(token.suffix) ||
            value.size() < token.prefix.size() + token.suffix.size()) return false;
        auto captured = value.substr(token.prefix.size(), value.size() - token.prefix.size() - token.suffix.size());
        if (!token.mappings.empty() && !token.mappings.contains(captured)) return false;
        captures.emplace(token.parameter, std::move(captured));
    }
    return true;
}
bool CommandPattern::Matches(std::string_view input) const {
    std::unordered_map<std::string, std::string> captures;
    std::string error;
    return Capture(input, captures, error);
}
CommandPattern CommandPattern::WithPattern(std::string pattern) const {
    return Compile(std::move(pattern), function_, fixedValues_, symbols_);
}
PatternMatch CommandPattern::Match(std::string_view input, const CommandContext& context) const {
    PatternMatch result;
    std::unordered_map<std::string, std::string> captures;
    result.matched = Capture(input, captures, result.error);
    if (!result.matched) return result;
    result.arguments = fixedValues_;
    try {
        for (const auto& token : tokens_) {
            if (token.parameter.empty()) continue;
            const auto& parameter = Parameter(function_, token.parameter);
            std::optional<CommandValue> value;
            if (token.mappings.empty()) value = ParseCommandValue(captures.at(token.parameter), token.captureType);
            else {
                const auto& mapping = token.mappings.at(captures.at(token.parameter));
                if (mapping.literal) value = mapping.literal;
                else {
                    const auto& symbol = symbols_.at(mapping.symbol);
                    value = ConvertCommandValue(symbol.read(), symbol.type);
                    if (value) value = ConvertCommandValue(*value, token.captureType);
                }
            }
            if (value) value = ConvertCommandValue(*value, parameter.type);
            if (!value) { result.error = "Invalid argument: " + token.parameter; return result; }
            result.arguments.emplace(token.parameter, std::move(*value));
        }
        for (const auto& parameter : function_.parameters) {
            if (result.arguments.contains(parameter.name)) continue;
            if (parameter.contextValue) {
                auto value = ConvertCommandValue(parameter.contextValue(context), parameter.type);
                if (!value) { result.error = "Invalid context argument: " + parameter.name; return result; }
                result.arguments.emplace(parameter.name, std::move(*value));
            } else if (parameter.defaultValue) result.arguments.emplace(parameter.name, *parameter.defaultValue);
        }
        result.valid = true;
    } catch (const std::exception& error) { result.error = error.what(); }
    catch (...) { result.error = "Argument provider failed"; }
    return result;
}

void CommandRegistry::Add(CommandBinding binding) {
    if (binding.bindingId.empty() || !binding.invoke || binding.pattern.Function().functionId.empty())
        throw std::invalid_argument("Binding requires identity, compiled pattern and invocation");
    if (std::any_of(bindings_.begin(), bindings_.end(), [&](const auto& item) { return item.bindingId == binding.bindingId; }))
        throw std::invalid_argument("Duplicate BindingId");
    bindings_.push_back(std::move(binding));
}
bool CommandRegistry::Remove(std::string_view bindingId) {
    auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const auto& binding) { return binding.bindingId == bindingId; });
    if (found == bindings_.end()) return false;
    bindings_.erase(found);
    return true;
}
CommandDispatchResult CommandRegistry::Dispatch(std::string_view input, const CommandContext& context) const {
    CommandDispatchResult result;
    // Copy the registration snapshot: a callback may modify the live registry.
    const auto snapshot = bindings_;
    std::vector<const CommandBinding*> candidates;
    for (const auto& binding : snapshot) {
        if (!binding.enabled) continue;
        if (binding.pattern.Matches(input)) candidates.push_back(&binding);
    }
    if (candidates.size() > 1)
        result.diagnostics.push_back("(" + candidates.front()->pattern.CommandName() +
            ") YouTubeController : Multiple controls detected from a single command)");
    for (const auto* candidate : candidates) {
        const auto& binding = *candidate;
        CommandDispatchItem item{ binding.bindingId, CommandDispatchStatus::Executed, {} };
        try {
            if (binding.available && !binding.available()) item.status = CommandDispatchStatus::Unavailable;
            else if (binding.authorized && !binding.authorized(context)) item.status = CommandDispatchStatus::Denied;
            else {
                auto match = binding.pattern.Match(input, context);
                if (!match.valid) { item.status = CommandDispatchStatus::InvalidArguments; item.detail = match.error; }
                else binding.invoke(match.arguments, context);
            }
        } catch (const std::exception& error) { item.status = CommandDispatchStatus::Failed; item.detail = error.what(); }
        catch (...) { item.status = CommandDispatchStatus::Failed; item.detail = "Command invocation failed"; }
        result.items.push_back(std::move(item));
    }
    return result;
}

} // namespace Rice::Streaming
