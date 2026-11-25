#pragma once
#include "Engine/Data/SqliteStore.h"
#include "Engine/Data/DataTypeSchema.h"
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <limits>
#include <list>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace Rice::Data {
namespace CodecDetail {
inline constexpr size_t MaximumBytes = 16 * 1024 * 1024;
inline constexpr size_t MaximumElements = 1024 * 1024;
struct Writer {
    std::vector<uint8_t> bytes;
    void Byte(uint8_t value) {
        if (bytes.size() == MaximumBytes) throw DataError(ErrorCode::InvalidValue, "Encoded value exceeds limit");
        bytes.push_back(value);
    }
    void Size(size_t value) {
        if (value > MaximumElements) throw DataError(ErrorCode::InvalidValue, "Container exceeds element limit");
        for (unsigned shift = 0; shift < 64; shift += 8) Byte(static_cast<uint8_t>(value >> shift));
    }
};
struct Reader {
    std::span<const uint8_t> bytes;
    size_t position = 0;
    uint8_t Byte() {
        if (position == bytes.size()) throw DataError(ErrorCode::InvalidValue, "Truncated encoded value");
        return bytes[position++];
    }
    size_t Size() {
        uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) result |= uint64_t(Byte()) << shift;
        if (result > MaximumElements) throw DataError(ErrorCode::InvalidValue, "Container exceeds element limit");
        return static_cast<size_t>(result);
    }
};
} // namespace CodecDetail

// Extend this trait for custom types. No raw-memory serialization is ever used.
template<class T, class Enable = void> struct DataCodec;
template<class T> void RegisterDataSchema();
template<class T> concept Encodable = requires(const T& value, CodecDetail::Writer& writer, CodecDetail::Reader& reader) {
    { DataCodec<T>::Type() } -> std::convertible_to<std::string>;
    { DataCodec<T>::Version } -> std::convertible_to<uint32_t>;
    { DataCodec<T>::Write(writer, value) } -> std::same_as<void>;
    { DataCodec<T>::Read(reader) } -> std::same_as<T>;
};
// Native modules may already have encoded their value using its registered
// type and version. Preserve that schema when routing through the engine.
template<class T> concept Routable = Encodable<T> || std::same_as<T,EncodedValue>;
template<class T> struct DataCodec<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static constexpr uint32_t Version = 1;
    static std::string Type() { return std::string(std::is_signed_v<T> ? "i" : "u") + std::to_string(sizeof(T) * 8); }
    static void Write(CodecDetail::Writer& writer, T value) {
        using U = std::make_unsigned_t<T>;
        U bits = std::bit_cast<U>(value);
        for (size_t index = 0; index < sizeof(T); ++index) writer.Byte(static_cast<uint8_t>(bits >> (index * 8)));
    }
    static T Read(CodecDetail::Reader& reader) {
        using U = std::make_unsigned_t<T>;
        U bits = 0;
        for (size_t index = 0; index < sizeof(T); ++index) bits |= static_cast<U>(static_cast<U>(reader.Byte()) << (index * 8));
        return std::bit_cast<T>(bits);
    }
};
template<> struct DataCodec<bool> {
    static constexpr uint32_t Version = 1;
    static std::string Type() { return "bool"; }
    static void Write(CodecDetail::Writer& writer, bool value) { writer.Byte(value ? 1 : 0); }
    static bool Read(CodecDetail::Reader& reader) {
        auto value = reader.Byte();
        if (value > 1) throw DataError(ErrorCode::InvalidValue, "Invalid encoded bool");
        return value != 0;
    }
};
template<class T> struct DataCodec<T, std::enable_if_t<std::is_floating_point_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)>> {
    static constexpr uint32_t Version = 1;
    static std::string Type() { return std::string("f") + std::to_string(sizeof(T) * 8); }
    using Bits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    static void Write(CodecDetail::Writer& writer, T value) {
        static_assert(std::numeric_limits<T>::is_iec559);
        if (!std::isfinite(value)) throw DataError(ErrorCode::InvalidValue, "Nonfinite number cannot be stored");
        DataCodec<Bits>::Write(writer, std::bit_cast<Bits>(value));
    }
    static T Read(CodecDetail::Reader& reader) {
        T value = std::bit_cast<T>(DataCodec<Bits>::Read(reader));
        if (!std::isfinite(value)) throw DataError(ErrorCode::InvalidValue, "Invalid nonfinite encoded number");
        return value;
    }
};
template<> struct DataCodec<std::string> {
    static constexpr uint32_t Version = 1;
    static std::string Type() { return "utf8"; }
    static bool ValidUtf8(std::string_view value) {
        size_t i = 0;
        while (i < value.size()) {
            uint8_t lead = static_cast<uint8_t>(value[i++]);
            if (lead < 128) continue;
            unsigned count; uint32_t code, minimum;
            if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 31; minimum = 0x80; }
            else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 15; minimum = 0x800; }
            else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 7; minimum = 0x10000; }
            else return false;
            while (count--) {
                if (i == value.size()) return false;
                uint8_t next = static_cast<uint8_t>(value[i++]);
                if ((next & 0xc0) != 0x80) return false;
                code = (code << 6) | (next & 63);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        }
        return true;
    }
    static void Write(CodecDetail::Writer& writer, const std::string& value) {
        if (!ValidUtf8(value)) throw DataError(ErrorCode::InvalidValue, "String is not UTF-8");
        writer.Size(value.size()); for (unsigned char byte : value) writer.Byte(byte);
    }
    static std::string Read(CodecDetail::Reader& reader) {
        auto size = reader.Size();
        if (size > reader.bytes.size() - reader.position) throw DataError(ErrorCode::InvalidValue, "Truncated string");
        std::string value; value.reserve(size);
        while (size--) value += static_cast<char>(reader.Byte());
        if (!ValidUtf8(value)) throw DataError(ErrorCode::InvalidValue, "Encoded string is not UTF-8");
        return value;
    }
};
template<Encodable T, size_t N> struct DataCodec<std::array<T, N>> {
    static constexpr uint32_t Version = 1;
    static void RegisterMembers() { RegisterDataSchema<T>(); }
    static std::string Type() { return "array(" + std::to_string(N) + "," + DataCodec<T>::Type() + ":" + std::to_string(DataCodec<T>::Version) + ")"; }
    static void Write(CodecDetail::Writer& writer, const std::array<T, N>& value) {
        static_assert(N <= CodecDetail::MaximumElements);
        for (const auto& item : value) DataCodec<T>::Write(writer, item);
    }
    static std::array<T, N> Read(CodecDetail::Reader& reader) {
        std::array<T, N> result{}; for (auto& item : result) item = DataCodec<T>::Read(reader); return result;
    }
};
template<Encodable T, class Allocator> struct DataCodec<std::vector<T, Allocator>> {
    static constexpr uint32_t Version = 1;
    static void RegisterMembers() { RegisterDataSchema<T>(); }
    static std::string Type() { return "list(" + DataCodec<T>::Type() + ":" + std::to_string(DataCodec<T>::Version) + ")"; }
    static void Write(CodecDetail::Writer& writer, const std::vector<T, Allocator>& value) {
        writer.Size(value.size()); for (const auto& item : value) DataCodec<T>::Write(writer, item);
    }
    static std::vector<T, Allocator> Read(CodecDetail::Reader& reader) {
        auto size = reader.Size(); std::vector<T, Allocator> result;
        result.reserve(size); while (size--) result.push_back(DataCodec<T>::Read(reader)); return result;
    }
};
template<glm::length_t N, Encodable T, glm::qualifier Q> struct DataCodec<glm::vec<N, T, Q>> {
    static constexpr uint32_t Version = 1;
    static std::string Type() { return "vec" + std::to_string(N) + "(" + DataCodec<T>::Type() + ")"; }
    static void Write(CodecDetail::Writer& writer, const glm::vec<N, T, Q>& value) {
        for (glm::length_t i = 0; i < N; ++i) DataCodec<T>::Write(writer, value[i]);
    }
    static glm::vec<N, T, Q> Read(CodecDetail::Reader& reader) {
        glm::vec<N, T, Q> result{}; for (glm::length_t i = 0; i < N; ++i) result[i] = DataCodec<T>::Read(reader); return result;
    }
};
template<Encodable T, class Allocator> struct DataCodec<std::list<T, Allocator>> {
    static constexpr uint32_t Version = 1;
    static void RegisterMembers() { RegisterDataSchema<T>(); }
    static std::string Type() { return "linked-list(" + DataCodec<T>::Type() + ":" + std::to_string(DataCodec<T>::Version) + ")"; }
    static void Write(CodecDetail::Writer& writer, const std::list<T, Allocator>& value) {
        writer.Size(value.size()); for (const auto& item : value) DataCodec<T>::Write(writer, item);
    }
    static std::list<T, Allocator> Read(CodecDetail::Reader& reader) {
        auto size = reader.Size(); std::list<T, Allocator> result;
        while (size--) result.push_back(DataCodec<T>::Read(reader)); return result;
    }
};
// A custom struct explicitly supplies stable SchemaId, Version, and a tuple of member pointers.
// Example: DataCodec<MyState> : StructDataCodec<MyState, &MyState::count, &MyState::position> {
//   static std::string Type() { return "my-project/state"; } static constexpr uint32_t Version = 1;
// };
// Changing fields requires a version bump and a migration; it never serializes struct padding.
template<class T, auto... Members> struct StructDataCodec {
    static void Write(CodecDetail::Writer& writer, const T& value) {
        (DataCodec<std::remove_cvref_t<decltype(value.*Members)>>::Write(writer, value.*Members), ...);
    }
    static T Read(CodecDetail::Reader& reader) {
        T result{};
        ((result.*Members = DataCodec<std::remove_cvref_t<decltype(result.*Members)>>::Read(reader)), ...);
        return result;
    }
};
template<class T> void RegisterDataSchema() {
    // A value tree may contain vector<T>; exporting its schema must not recurse forever.
    static thread_local bool registering = false;
    if (registering) return;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{registering};
    registering = true;
    if constexpr (requires { DataCodec<T>::RegisterMembers(); }) DataCodec<T>::RegisterMembers();
    if constexpr (requires { DataCodec<T>::Schema(); }) CompiledDataSchemas().Register(DataCodec<T>::Schema());
}
template<Encodable T> EncodedValue EncodeValue(const T& value) {
    RegisterDataSchema<T>();
    CodecDetail::Writer writer; DataCodec<T>::Write(writer, value);
    return {DataCodec<T>::Type(), DataCodec<T>::Version, std::move(writer.bytes)};
}
template<Encodable T> T DecodeValue(const EncodedValue& encoded) {
    if (encoded.type != DataCodec<T>::Type() || encoded.codecVersion != DataCodec<T>::Version)
        throw DataError(ErrorCode::SchemaMismatch, "Stored type/codec does not match requested value");
    if (encoded.bytes.size() > CodecDetail::MaximumBytes) throw DataError(ErrorCode::InvalidValue, "Encoded data exceeds limit");
    CodecDetail::Reader reader{encoded.bytes}; T result = DataCodec<T>::Read(reader);
    if (reader.position != reader.bytes.size()) throw DataError(ErrorCode::InvalidValue, "Trailing encoded data");
    return result;
}
inline EncodedValue EncodeValue(const EncodedValue& value) {
    if (value.type.empty() || value.type.size() > 1024 || !value.codecVersion ||
        value.bytes.size() > CodecDetail::MaximumBytes)
        throw DataError(ErrorCode::InvalidValue, "Invalid pre-encoded data value");
    return value;
}
template<class T> requires std::same_as<T,EncodedValue>
T DecodeValue(const EncodedValue& value) { return EncodeValue(value); }
} // namespace Rice::Data
