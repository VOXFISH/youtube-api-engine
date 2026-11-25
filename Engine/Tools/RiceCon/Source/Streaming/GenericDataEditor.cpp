#include "GenericDataEditor.h"
#include "Engine/Data/DataValueCodec.h"
#include "Json.h"
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>

namespace RiceController::Streaming::GenericDataEditor {
namespace D = Rice::Data;
namespace {
constexpr size_t MaximumEditorElements = 10000;
enum class Kind { Number, Boolean, String, Vector, Array, List, Struct };
struct Node {
    Kind kind;
    std::string type;
    uint32_t version = 1;
    size_t length = 0;
    std::vector<Node> children;
    std::vector<std::string> names;
};
template<class T> T ParseNumber(std::string_view text) {
    T result{};
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("숫자의 형식 또는 범위가 올바르지 않습니다.");
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(result)) throw std::runtime_error("유한한 숫자만 저장할 수 있습니다.");
    return result;
}
template<class T> Json Number(T value) {
    std::array<char, 128> buffer{};
    std::to_chars_result result;
    if constexpr (std::is_floating_point_v<T>)
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general,
                               std::numeric_limits<T>::max_digits10);
    else result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) throw std::runtime_error("숫자 표시 변환에 실패했습니다.");
    return Json::ExactNumber{std::string(buffer.data(), result.ptr)};
}
std::optional<Node> Type(const std::string& type, uint32_t version, const D::DataSchemaRegistry& schemas, unsigned depth);
std::optional<Node> VersionedElement(std::string_view text, const D::DataSchemaRegistry& schemas, unsigned depth) {
    auto delimiter = text.rfind(':');
    if (delimiter == std::string_view::npos) return {};
    try {
        auto version = ParseNumber<uint32_t>(text.substr(delimiter + 1));
        return Type(std::string(text.substr(0, delimiter)), version, schemas, depth + 1);
    } catch (const std::exception&) { return {}; }
}
std::optional<Node> Type(const std::string& type, uint32_t version, const D::DataSchemaRegistry& schemas, unsigned depth) {
    if (type.empty() || type.size() > 1024 || !version || depth > 16) return {};
    Node result{Kind::Number, type, version};
    if (version == 1) {
        if (type == "bool") { result.kind = Kind::Boolean; return result; }
        if (type == "utf8") { result.kind = Kind::String; return result; }
        for (auto scalar : {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64"})
            if (type == scalar) return result;
        if (type.size() > 6 && type.starts_with("vec") && type[3] >= '2' && type[3] <= '4' && type[4] == '(' && type.back() == ')') {
            auto element = Type(type.substr(5, type.size() - 6), 1, schemas, depth + 1);
            if (!element || element->kind != Kind::Number) return {};
            result.kind = Kind::Vector; result.length = static_cast<size_t>(type[3] - '0');
            result.children.push_back(std::move(*element)); return result;
        }
        size_t offset = type.starts_with("list(") ? 5 : type.starts_with("linked-list(") ? 12 : 0;
        if (offset && type.back() == ')') {
            auto element = VersionedElement(std::string_view(type).substr(offset, type.size() - offset - 1), schemas, depth);
            if (!element) return {};
            result.kind = Kind::List; result.children.push_back(std::move(*element)); return result;
        }
        if (type.starts_with("array(") && type.back() == ')') {
            auto inner = std::string_view(type).substr(6, type.size() - 7);
            auto delimiter = inner.find(',');
            if (delimiter == std::string_view::npos) return {};
            try { result.length = ParseNumber<size_t>(inner.substr(0, delimiter)); }
            catch (const std::exception&) { return {}; }
            if (result.length > MaximumEditorElements) return {};
            auto element = VersionedElement(inner.substr(delimiter + 1), schemas, depth);
            if (!element) return {};
            result.kind = Kind::Array; result.children.push_back(std::move(*element)); return result;
        }
    }
    if (auto schema = schemas.Find(type, version)) {
        result.kind = Kind::Struct;
        for (const auto& member : schema->members) {
            auto child = Type(member.type, member.codecVersion, schemas, depth + 1);
            if (!child) return {};
            result.names.push_back(member.name); result.children.push_back(std::move(*child));
        }
        return result;
    }
    return {};
}
Json Decode(const Node& node, D::CodecDetail::Reader& reader) {
    if (node.kind == Kind::Boolean) return D::DataCodec<bool>::Read(reader);
    if (node.kind == Kind::String) return D::DataCodec<std::string>::Read(reader);
    if (node.kind == Kind::Number) {
#define RICE_EDITOR_READ(Tag, TypeName) if (node.type == Tag) return Number(D::DataCodec<TypeName>::Read(reader))
        RICE_EDITOR_READ("i8", int8_t); RICE_EDITOR_READ("u8", uint8_t);
        RICE_EDITOR_READ("i16", int16_t); RICE_EDITOR_READ("u16", uint16_t);
        RICE_EDITOR_READ("i32", int32_t); RICE_EDITOR_READ("u32", uint32_t);
        RICE_EDITOR_READ("i64", int64_t); RICE_EDITOR_READ("u64", uint64_t);
        RICE_EDITOR_READ("f32", float); RICE_EDITOR_READ("f64", double);
#undef RICE_EDITOR_READ
    }
    if (node.kind == Kind::Struct) {
        Json::Object object;
        for (size_t i = 0; i < node.children.size(); ++i) object.emplace(node.names[i], Decode(node.children[i], reader));
        return object;
    }
    auto count = node.kind == Kind::List ? reader.Size() : node.length;
    if (count > MaximumEditorElements) throw std::runtime_error("값 편집은 목록당 10,000개 항목까지 지원합니다.");
    Json::Array values; values.reserve(count);
    for (size_t i = 0; i < count; ++i) values.push_back(Decode(node.children.front(), reader));
    return values;
}
void Encode(const Node& node, const Json& value, D::CodecDetail::Writer& writer) {
    if (node.kind == Kind::Boolean) { D::DataCodec<bool>::Write(writer, value.Boolean()); return; }
    if (node.kind == Kind::String) { D::DataCodec<std::string>::Write(writer, value.String()); return; }
    if (node.kind == Kind::Number) {
        auto number = std::get_if<Json::ExactNumber>(&value.value);
        if (!number) throw std::runtime_error("이 항목에는 JSON 숫자를 입력하세요. 문자열은 사용할 수 없습니다.");
#define RICE_EDITOR_WRITE(Tag, TypeName) if (node.type == Tag) { D::DataCodec<TypeName>::Write(writer, ParseNumber<TypeName>(number->text)); return; }
        RICE_EDITOR_WRITE("i8", int8_t); RICE_EDITOR_WRITE("u8", uint8_t);
        RICE_EDITOR_WRITE("i16", int16_t); RICE_EDITOR_WRITE("u16", uint16_t);
        RICE_EDITOR_WRITE("i32", int32_t); RICE_EDITOR_WRITE("u32", uint32_t);
        RICE_EDITOR_WRITE("i64", int64_t); RICE_EDITOR_WRITE("u64", uint64_t);
        RICE_EDITOR_WRITE("f32", float); RICE_EDITOR_WRITE("f64", double);
#undef RICE_EDITOR_WRITE
    }
    if (node.kind == Kind::Struct) {
        const auto& fields = value.Fields();
        if (fields.size() != node.names.size()) throw std::runtime_error("구조체의 필드 개수가 스키마와 다릅니다.");
        for (size_t i = 0; i < node.children.size(); ++i) Encode(node.children[i], value.At(node.names[i]), writer);
        return;
    }
    const auto& values = value.Items();
    if (values.size() > MaximumEditorElements) throw std::runtime_error("값 편집은 목록당 10,000개 항목까지 지원합니다.");
    if (node.kind != Kind::List && values.size() != node.length)
        throw std::runtime_error("벡터 또는 고정 배열의 길이가 형식과 다릅니다.");
    if (node.kind == Kind::List) writer.Size(values.size());
    for (const auto& item : values) Encode(node.children.front(), item, writer);
}
}
std::optional<std::string> ToText(const D::EncodedValue& value, const D::DataSchemaRegistry& schemas) {
    auto node = Type(value.type, value.codecVersion, schemas, 0);
    if (!node) return {};
    if (value.bytes.size() > D::CodecDetail::MaximumBytes) throw std::runtime_error("저장된 값이 크기 제한을 초과했습니다.");
    D::CodecDetail::Reader reader{value.bytes};
    auto result = Decode(*node, reader);
    if (reader.position != reader.bytes.size()) throw std::runtime_error("저장된 값과 스키마의 길이가 일치하지 않습니다.");
    auto text = node->kind == Kind::String ? result.String() : result.Dump();
    if (node->kind != Kind::String && text.size() > 4 * 1024 * 1024)
        throw std::runtime_error("편집용 JSON이 4 MiB 제한을 초과했습니다.");
    return text;
}
D::EncodedValue FromText(const std::string& type, uint32_t version, const std::string& text, const D::DataSchemaRegistry& schemas) {
    auto node = Type(type, version, schemas, 0);
    if (!node) throw std::runtime_error("이 값의 스키마가 등록되지 않아 편집할 수 없습니다.");
    D::CodecDetail::Writer writer;
    auto value = node->kind == Kind::String ? Json(text) : Json::ParseExactNumbers(text);
    Encode(*node, value, writer);
    return {type, version, std::move(writer.bytes)};
}
bool UsesJson(const std::string& type, uint32_t version, const D::DataSchemaRegistry& schemas) {
    auto node = Type(type, version, schemas, 0);
    return node && (node->kind == Kind::Vector || node->kind == Kind::Array || node->kind == Kind::List || node->kind == Kind::Struct);
}
}
