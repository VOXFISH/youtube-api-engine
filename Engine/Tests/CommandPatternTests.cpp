#include "Streaming/CommandPattern.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Rice::Streaming;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F&& fn, const char* message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
CommandFunctionDescriptor Movement() {
    return { "ChangeDirection", {
        { "direction", CommandValueType::Int32, {}, {} },
        { "steps", CommandValueType::Int32, {}, {} },
        { "showEffect", CommandValueType::Bool, CommandValue(true), {} }
    }};
}
void UserPatternAndNamedArgs() {
    auto pattern = CommandPattern::Compile(
        R"([YouTubeControllable : "!회피 {int direction : (좌, -1), (중, 0), (우, 1)} {steps}칸"])", Movement());
    auto result = pattern.Match("  !회피\t좌   3칸  ");
    Check(result.matched && result.valid, "User Korean mapping/suffix pattern matches");
    Check(std::get<std::int64_t>(result.arguments.at("direction")) == -1 &&
        std::get<std::int64_t>(result.arguments.at("steps")) == 3 &&
        std::get<bool>(result.arguments.at("showEffect")), "Named arguments and C++ default metadata");
    Check(!pattern.Match("!회피 상 3칸").matched, "Unknown mapping key ignores pattern");
    Check(!pattern.Match("!회피 좌 3").matched, "Suffix is mandatory");
    auto bad = pattern.Match("!회피 좌 2147483648칸");
    Check(bad.matched && !bad.valid, "Int32 overflow is rejected, not wrapped");
    Check(!pattern.Match("!회피 좌 3.5칸").valid, "Fractional integer is rejected");
    auto reordered = CommandPattern::Compile("!이동 {steps} {direction}", Movement(), {{"showEffect", false}});
    auto reorderedResult = reordered.Match("!이동 5 -1");
    Check(reorderedResult.valid && std::get<std::int64_t>(reorderedResult.arguments.at("steps")) == 5 &&
        !std::get<bool>(reorderedResult.arguments.at("showEffect")), "Names override function argument order; fixed overrides default");
    auto raw = CommandPattern::Compile("[YouTubeControllable : !회피 {direction (좌, -1), (우, 1)} {steps}칸]", Movement());
    Check(raw.Match("!회피 우 4칸").valid, "Unquoted annotation and colon-free compatibility mapping");
}
void StringsContextsAndSymbols() {
    CommandFunctionDescriptor function{ "Greet", {
        { "username", CommandValueType::String, {}, {} },
        { "channel", CommandValueType::String, {}, [](const CommandContext& context) { return CommandValue(context.authorChannelId); } }
    }};
    auto pattern = CommandPattern::Compile("!게임 인사 {username}", function);
    CommandContext context; context.authorChannelId = "verified-channel";
    auto result = pattern.Match("!게임 인사 \"Jane Doe\"", context);
    Check(result.valid && std::get<std::string>(result.arguments.at("username")) == "Jane Doe" &&
        std::get<std::string>(result.arguments.at("channel")) == "verified-channel", "Quoted token and explicit verified context binding");
    Check(!pattern.Match("!게임 인사 \"unfinished", context).matched, "Unclosed input quote is rejected");
    int current = -2;
    CommandSymbols symbols{{ "LeftValue", {CommandValueType::Int32, [&] { return CommandValue(std::int64_t(current)); }} }};
    auto dynamic = CommandPattern::Compile("!회피 {direction : (좌, LeftValue)} {steps}칸", Movement(), {}, symbols);
    current = -3;
    Check(std::get<std::int64_t>(dynamic.Match("!회피 좌 2칸").arguments.at("direction")) == -3, "Registered symbols read at dispatch time");
    auto typed = CommandPattern::Compile("!회피 {std::int32_t direction} {steps}칸", Movement());
    Check(typed.Match("!회피 -1 2칸").valid, "Namespace type colon does not start mapping");
}
void SchemaValidation() {
    Reject([] { CommandPattern::Compile("!회피 {missing} {steps}", Movement()); }, "Unknown parameter rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction}", Movement()); }, "Missing required parameter rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction} {direction} {steps}", Movement()); }, "Duplicate capture rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction : (좌, -1), (좌, 1)} {steps}", Movement()); }, "Duplicate mapping key rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction : (좌, UnknownValue)} {steps}", Movement()); }, "Unknown symbol rejected");
    Reject([] { CommandPattern::Compile("!회피 {std::string direction} {steps}", Movement()); }, "Non-convertible annotation type rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction}{steps}", Movement()); }, "Ambiguous adjacent captures rejected");
    Reject([] { CommandPattern::Compile("!회피 {direction : (좌, 999999999999)} {steps}", Movement()); }, "Out-of-range mapping constant rejected during registration");
    Reject([] { CommandPattern::Compile("!회피 {direction} {steps", Movement()); }, "Malformed capture rejected");
}
void NumericBoundaries() {
    Check(!ParseCommandValue("+-1", CommandValueType::Int32), "Multiple signs rejected");
    Check(!ParseCommandValue("NaN", CommandValueType::Double), "NaN rejected");
    Check(!ParseCommandValue("1e999", CommandValueType::Double), "Infinity overflow rejected");
    Check(!ParseCommandValue("-1", CommandValueType::UInt64), "Unsigned negative rejected");
    Check(ParseCommandValue("18446744073709551615", CommandValueType::UInt64).has_value(), "UInt64 maximum parses without double loss");
    Check(!ConvertCommandValue(CommandValue(std::numeric_limits<std::uint64_t>::max()), CommandValueType::Double), "Lossy integer-to-double conversion rejected");
    Check(!ConvertCommandValue(CommandValue(9223372036854775808.0), CommandValueType::Int64), "Double INT64 boundary rejected safely");
    Check(ParseCommandValue("0.1", CommandValueType::Float).has_value(), "Float input parsed at float precision");
    Check(!ConvertCommandValue(CommandValue(std::int64_t(16777217)), CommandValueType::Float), "Lossy integer-to-float conversion rejected");
}
void MultipleControlsPermissionsAndMutation() {
    auto pattern = CommandPattern::Compile("!회피 {direction} {steps}", Movement());
    CommandRegistry registry;
    std::vector<int> order;
    registry.Add({ "first", pattern, [&](const auto&, const auto&) { order.push_back(1); registry.Clear(); }, {}, {}, true });
    registry.Add({ "denied", pattern, [&](const auto&, const auto&) { order.push_back(2); }, [](const auto&) { return false; }, {}, true });
    registry.Add({ "third", pattern, [&](const auto&, const auto&) { order.push_back(3); }, {}, {}, true });
    auto result = registry.Dispatch("!회피 -1 3");
    Check(order == std::vector<int>{1, 3} && result.items.size() == 3, "All matching controls execute in snapshot order; denied one does not hide later match");
    Check(result.items[1].status == CommandDispatchStatus::Denied, "Per-binding permission enforced");
    Check(result.diagnostics == std::vector<std::string>{"(!회피) YouTubeController : Multiple controls detected from a single command)"}, "Exact requested multiple-control diagnostic");
    Check(registry.Count() == 0, "Registry mutation affects subsequent dispatch only");
    int reads = 0;
    CommandSymbols symbols{{ "Left", {CommandValueType::Int32, [&] { ++reads; return CommandValue(std::int64_t(-1)); }} }};
    auto restricted = CommandPattern::Compile("!회피 {direction : (좌, Left)} {steps}", Movement(), {}, symbols);
    registry.Add({ "denied", restricted, [](const auto&, const auto&) {}, [](const auto&) { return false; }, {}, true });
    registry.Dispatch("!회피 좌 1");
    Check(reads == 0, "Denied command does not read runtime data symbols");
    registry.Clear();
    registry.Add({ "bad", pattern, [](const auto&, const auto&) { throw std::runtime_error("failure"); }, {}, {}, true });
    registry.Add({ "good", pattern, [&](const auto&, const auto&) { order.push_back(4); }, {}, {}, true });
    result = registry.Dispatch("!회피 1 2");
    Check(result.items[0].status == CommandDispatchStatus::Failed && order.back() == 4, "Failure does not suppress later valid control");
}
}
int main() {
    try {
        UserPatternAndNamedArgs(); StringsContextsAndSymbols(); SchemaValidation();
        NumericBoundaries(); MultipleControlsPermissionsAndMutation();
        std::cout << "Command pattern tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Command pattern test failed: " << error.what() << '\n';
        return 1;
    }
}
