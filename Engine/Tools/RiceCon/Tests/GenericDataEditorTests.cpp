#include "Streaming/GenericDataEditor.h"
#include "Engine/Data/DataField.h"
#include "Engine/Serialization/Json.h"
#include <iostream>
#include <limits>

using Names = std::vector<std::string>;
struct EditorPayload { int count = 0; glm::vec3 position{0}; Names names; };
#define EDITOR_PAYLOAD_FIELDS(X) X(int, count) X(glm::vec3, position) X(Names, names)
RICE_DATA_STRUCT(EditorPayload, "editor-tests/payload", 2, EDITOR_PAYLOAD_FIELDS)

namespace D = Rice::Data;
namespace E = RiceController::Streaming::GenericDataEditor;
using Json = Rice::Serialization::Json;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void Reject(F&& function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid editor value was accepted");
}
template<D::Encodable T> void Roundtrip(const T& value) {
    auto original = D::EncodeValue(value); auto text = E::ToText(original);
    Check(text.has_value(), "Known type was marked read-only");
    Check(E::FromText(original.type, original.codecVersion, *text) == original, "Editor roundtrip changed typed bytes");
}
void NumericContracts() {
    Reject([] { Json::Parse("1.5"); });
    Reject([] { Json::Parse("18446744073709551615"); });
    auto precise = Json::ParseExactNumbers("[18446744073709551615,1.25e-20,-0.0]");
    Check(precise.Dump() == "[18446744073709551615,1.25e-20,-0.0]", "Numeric lexemes changed");
    Check(Json::Parse("{\"n\":3}").At("n").Integer() == 3, "Default integer JSON contract changed");
    for (auto bad : {"01", "1.", "1e", "1e+", "NaN", "Infinity", "-NaN", "--1", "+1"})
        Reject([&] { Json::ParseExactNumbers(bad); });
    Roundtrip(UINT64_MAX); Roundtrip(INT64_MIN); Roundtrip(std::numeric_limits<double>::max());
    Roundtrip(std::numeric_limits<float>::lowest()); Roundtrip(-0.0f);
    Check(D::DecodeValue<uint64_t>(E::FromText("u64", 1, "18446744073709551615")) == UINT64_MAX, "uint64 maximum lost precision");
    Check(D::DecodeValue<int8_t>(E::FromText("i8", 1, "-128")) == -128, "Small signed integer parse");
    for (auto value : {"18446744073709551616", "-1", "\"10\"", "1.0", "true"})
        Reject([&] { E::FromText("u64", 1, value); });
    Reject([] { E::FromText("i64", 1, "9223372036854775808"); });
    Reject([] { E::FromText("i32", 1, "2147483648"); });
    Reject([] { E::FromText("i8", 1, "128"); });
    Reject([] { E::FromText("u8", 1, "256"); });
    Reject([] { E::FromText("f32", 1, "3.5e38"); });
    Reject([] { E::FromText("f64", 1, "1e309"); });
    Reject([] { E::FromText("bool", 1, "1"); });
}
void CompositeValues() {
    Roundtrip(glm::vec2(1.25f, -2.5f)); Roundtrip(glm::vec3(1.5f, 2.75f, -4.f));
    Roundtrip(glm::vec4(.25f, .5f, .75f, 1.f));
    Roundtrip(std::array<uint64_t, 2>{0, UINT64_MAX});
    Roundtrip(std::array<std::array<int, 2>, 2>{{{1, 2}, {3, 4}}});
    Roundtrip(std::vector<glm::vec3>{{1, 2, 3}, {4, 5, 6}});
    Roundtrip(std::vector<std::vector<float>>{{1.25f, -2.f}, {}, {3.5f}});
    Roundtrip(std::list<double>{1.0, -2.125});
    Roundtrip(std::vector<bool>{true, false, true});
    Roundtrip(Names{"hello", "quote \" slash \\ newline\n", "\xED\x95\x9C\xEA\xB8\x80"});
    Roundtrip(std::array<int, 0>{});
    Check(E::UsesJson("vec3(f32)", 1) && !E::UsesJson("utf8", 1), "JSON editing mode");
    Check(D::DecodeValue<glm::vec3>(E::FromText("vec3(f32)", 1, "[1, 2.5, -3e0]")) == glm::vec3(1, 2.5f, -3), "Vector edit");
    Reject([] { E::FromText("vec3(f32)", 1, "[1,2]"); });
    Reject([] { E::FromText("vec2(f32)", 1, "[1,2,3]"); });
    Reject([] { E::FromText("vec2(f32)", 1, "[1,\"2\"]"); });
    Reject([] { E::FromText("array(2,array(2,i32:1):1)", 1, "[[1,2],[3]]"); });
    Reject([] { E::FromText("list(i32:1)", 1, "[1,2.5]"); });
    Reject([] { E::FromText("list(bool:1)", 1, "[true,0]"); });
    Reject([] { E::FromText("list(utf8:1)", 1, "[\"ok\",1]"); });
    Reject([] { E::FromText("array(-1,i32:1)", 1, "[]"); });
    Reject([] { E::FromText("array(10001,i32:1)", 1, "[]"); });
    Reject([] { E::FromText("vec3(utf8)", 1, "[\"a\",\"b\",\"c\"]"); });
    auto damaged = D::EncodeValue(glm::vec3(0)); damaged.bytes.pop_back();
    Reject([&] { E::ToText(damaged); });
    damaged = D::EncodeValue(glm::vec2(0)); damaged.bytes.push_back(0);
    Reject([&] { E::ToText(damaged); });
}
void StructSchemas() {
    EditorPayload payload{7, {1, 2, 3}, {"one", "two"}}; auto encoded = D::EncodeValue(payload);
    Roundtrip(payload);
    D::DataSchemaRegistry exported;
    for (auto schema : D::CompiledDataSchemas().Export()) exported.Register(std::move(schema));
    auto text = E::ToText(encoded, exported);
    Check(text && text->find("\"count\":7") != std::string::npos, "Compiled struct schema was not exported");
    auto edited = E::FromText(encoded.type, encoded.codecVersion, "{\"names\":[\"new\"],\"position\":[4,5,6],\"count\":9}", exported);
    auto value = D::DecodeValue<EditorPayload>(edited);
    Check(value.count == 9 && value.position == glm::vec3(4,5,6) && value.names == Names{"new"}, "Struct fields did not follow wire schema order");
    Reject([&] { E::FromText(encoded.type, 2, "{\"count\":1,\"position\":[0,0,0]}", exported); });
    Reject([&] { E::FromText(encoded.type, 2, "{\"count\":1,\"position\":[0,0,0],\"names\":[],\"extra\":1}", exported); });
    Reject([&] { E::FromText(encoded.type, 2, "{\"count\":\"1\",\"position\":[0,0,0],\"names\":[]}", exported); });
    D::DataSchemaRegistry empty;
    Check(!E::ToText(encoded, empty), "Unknown struct must remain read-only");
    Check(!E::ToText(D::EncodedValue{"unregistered", 1, {0,0,0,0}}, empty), "Raw bytes were guessed");
    Reject([&] { E::FromText("unregistered", 1, "0", empty); });
    Check(!E::ToText(D::EncodedValue{"i32", 2, {0,0,0,0}}, empty), "Unsupported codec version was edited");
    auto conflict = *exported.Find(encoded.type, 2); conflict.members.front().type = "utf8";
    Reject([&] { exported.Register(conflict); });
    D::DataSchemaRegistry recursive;
    recursive.Register({"recursive", 1, {{"child", "recursive", 1}}});
    Check(!E::ToText(D::EncodedValue{"recursive", 1, {}}, recursive), "Recursive unknown shape must be bounded");
}
}
int main() {
    try {
        NumericContracts(); CompositeValues(); StructSchemas();
        std::cout << "Generic data editor exact numbers, vectors, nested containers and schema tests passed\n";
        return 0;
    } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
