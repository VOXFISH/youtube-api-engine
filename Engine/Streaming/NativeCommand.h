#pragma once

#include "CommandPattern.h"

#include <array>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Rice::Streaming {
namespace NativeCommandDetail {
template<class> inline constexpr bool Unsupported = false;
template<class T> constexpr CommandValueType ValueType() {
    using Value = std::remove_cvref_t<T>;
    static_assert(!std::is_reference_v<T> || (std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>),
        "YouTube command parameters must be values or const lvalue references");
    if constexpr (std::is_same_v<Value, bool>) return CommandValueType::Bool;
    else if constexpr (std::is_same_v<Value, std::string>) return CommandValueType::String;
    else if constexpr (std::is_same_v<Value, float>) return CommandValueType::Float;
    else if constexpr (std::is_same_v<Value, double>) return CommandValueType::Double;
    else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value> && sizeof(Value) == 4) return CommandValueType::Int32;
    else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value> && sizeof(Value) == 8) return CommandValueType::Int64;
    else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value> && sizeof(Value) == 4) return CommandValueType::UInt32;
    else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value> && sizeof(Value) == 8) return CommandValueType::UInt64;
    else static_assert(Unsupported<Value>, "Unsupported native YouTube argument; register a supported typed wrapper");
}
template<class T> CommandValue Box(T&& value) {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, bool>) return value;
    else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) return std::int64_t(value);
    else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value>) return std::uint64_t(value);
    else if constexpr (std::is_floating_point_v<Value>) return double(value);
    else if constexpr (std::is_convertible_v<T, std::string_view>) return std::string(std::string_view(value));
    else static_assert(Unsupported<Value>, "Unsupported native YouTube value");
}
template<class T> std::remove_cvref_t<T> Read(const CommandValue& value) {
    using Value = std::remove_cvref_t<T>;
    auto converted = ConvertCommandValue(value, ValueType<T>());
    if (!converted) throw std::invalid_argument("Native command argument conversion failed");
    if constexpr (std::is_same_v<Value, std::string>) return std::get<std::string>(*converted);
    else if constexpr (std::is_same_v<Value, bool>) return std::get<bool>(*converted);
    else if constexpr (std::is_floating_point_v<Value>) return static_cast<Value>(std::get<double>(*converted));
    else if constexpr (std::is_signed_v<Value>) return static_cast<Value>(std::get<std::int64_t>(*converted));
    else return static_cast<Value>(std::get<std::uint64_t>(*converted));
}
template<class T> struct Traits;
template<class R, class... Args> struct Traits<R(*)(Args...)> {
    using Arguments = std::tuple<Args...>;
    static constexpr bool member = false;
};
template<class R, class... Args> struct Traits<R(*)(Args...) noexcept> : Traits<R(*)(Args...)> {};
template<class C, class R, class... Args> struct Traits<R(C::*)(Args...)> {
    using Arguments = std::tuple<Args...>;
    using Class = C;
    static constexpr bool member = true;
};
template<class C, class R, class... Args> struct Traits<R(C::*)(Args...) const> : Traits<R(C::*)(Args...)> {};
template<class C, class R, class... Args> struct Traits<R(C::*)(Args...) noexcept> : Traits<R(C::*)(Args...)> {};
template<class C, class R, class... Args> struct Traits<R(C::*)(Args...) const noexcept> : Traits<R(C::*)(Args...)> {};
}

template<class T> struct NativeParameter {
    using Value = T;
    std::string name;
    std::optional<CommandValue> defaultValue;
    template<class U> NativeParameter& Default(U&& value) {
        static_assert(std::is_convertible_v<U, T>, "Registration default must be C++ implicitly convertible to parameter type");
        defaultValue = NativeCommandDetail::Box(std::forward<U>(value));
        return *this;
    }
};
template<class T> NativeParameter<T> Parameter(std::string name) { return { std::move(name), {} }; }

struct NativeCommandOptions {
    NamedArguments fixedValues;
    CommandSymbols symbols;
    std::function<bool(const CommandContext&)> authorized;
};

namespace NativeCommandDetail {
template<class ArgumentTuple, class ParameterTuple, std::size_t... Index>
CommandFunctionDescriptor Descriptor(std::string functionId, const ParameterTuple& parameters, std::index_sequence<Index...>) {
    static_assert((std::is_convertible_v<typename std::tuple_element_t<Index, ParameterTuple>::Value,
        std::tuple_element_t<Index, ArgumentTuple>> && ...),
        "Registered parameter types must be implicitly convertible to native function arguments");
    return { std::move(functionId), {
        CommandParameter{ std::get<Index>(parameters).name, ValueType<std::tuple_element_t<Index, ArgumentTuple>>(),
            std::get<Index>(parameters).defaultValue, {} }...
    }};
}
template<class ArgumentTuple, class ParameterTuple, std::size_t... Index>
auto ReadArguments(const NamedArguments& values, const ParameterTuple& parameters, std::index_sequence<Index...>) {
    return std::tuple<std::remove_cvref_t<std::tuple_element_t<Index, ArgumentTuple>>...>{
        Read<std::tuple_element_t<Index, ArgumentTuple>>(values.at(std::get<Index>(parameters).name))...
    };
}
}

template<auto Function, class... Parameters>
CommandBinding MakeNativeBinding(std::string bindingId, std::string functionId, std::string pattern,
    std::tuple<Parameters...> parameters, NativeCommandOptions options = {}) {
    using Traits = NativeCommandDetail::Traits<decltype(Function)>;
    static_assert(!Traits::member, "Use MakeNativeMemberBinding for member functions");
    using Arguments = typename Traits::Arguments;
    static_assert(sizeof...(Parameters) == std::tuple_size_v<Arguments>, "Register every native argument name; defaults must be explicit");
    constexpr auto indices = std::make_index_sequence<sizeof...(Parameters)>{};
    auto descriptor = NativeCommandDetail::Descriptor<Arguments>(std::move(functionId), parameters, indices);
    auto compiled = CommandPattern::Compile(std::move(pattern), std::move(descriptor),
        std::move(options.fixedValues), std::move(options.symbols));
    return { std::move(bindingId), std::move(compiled),
        [parameters = std::move(parameters)](const NamedArguments& values, const CommandContext&) {
            auto arguments = NativeCommandDetail::ReadArguments<Arguments>(values, parameters, indices);
            std::apply(Function, arguments);
        }, std::move(options.authorized), {}, true };
}

// The resolver is called immediately before each invocation. It must resolve a
// live object on the owner thread, not return a cached dangling component pointer.
template<auto Method, class Resolver, class... Parameters>
CommandBinding MakeNativeMemberBinding(std::string bindingId, std::string functionId, std::string pattern,
    Resolver resolve, std::tuple<Parameters...> parameters, NativeCommandOptions options = {}) {
    using Traits = NativeCommandDetail::Traits<decltype(Method)>;
    static_assert(Traits::member, "Expected a member-function pointer");
    using Arguments = typename Traits::Arguments;
    static_assert(sizeof...(Parameters) == std::tuple_size_v<Arguments>, "Register every native argument name; defaults must be explicit");
    constexpr auto indices = std::make_index_sequence<sizeof...(Parameters)>{};
    auto descriptor = NativeCommandDetail::Descriptor<Arguments>(std::move(functionId), parameters, indices);
    auto compiled = CommandPattern::Compile(std::move(pattern), std::move(descriptor),
        std::move(options.fixedValues), std::move(options.symbols));
    auto invoke = [resolve, parameters = std::move(parameters)](const NamedArguments& values, const CommandContext&) {
        auto target = resolve();
        if (!target) throw std::runtime_error("Native command target is no longer available");
        auto arguments = NativeCommandDetail::ReadArguments<Arguments>(values, parameters, indices);
        std::apply([&](auto&... args) { std::invoke(Method, *target, args...); }, arguments);
    };
    return { std::move(bindingId), std::move(compiled), std::move(invoke),
        std::move(options.authorized), [resolve] { return resolve() != nullptr; }, true };
}

} // namespace Rice::Streaming

// Reuse one comma-separated X-macro list for the actual C++ declaration and
// registration metadata. This shares names without pretending C++20 reflection.
// #define MOVE_ARGUMENTS(X) X(int, direction), X(int, steps)
#define RICE_YOUTUBE_DECLARE_PARAMETER(type, name) type name
#define RICE_YOUTUBE_BIND_PARAMETER(type, name) ::Rice::Streaming::Parameter<type>(#name)
#define RICE_YOUTUBE_DECLARE_PARAMETERS(list) list(RICE_YOUTUBE_DECLARE_PARAMETER)
#define RICE_YOUTUBE_PARAMETERS(list) std::tuple{ list(RICE_YOUTUBE_BIND_PARAMETER) }
