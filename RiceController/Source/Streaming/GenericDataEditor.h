#pragma once
#include "Engine/Data/DataTypeSchema.h"
#include <optional>
#include <string>

namespace RiceController::Streaming::GenericDataEditor {
// Scalar strings use plain text; composite values use JSON. Exact numeric parsing keeps
// uint64 and floating-point values lossless and does not alter settings/protocol JSON rules.
std::optional<std::string> ToText(const Rice::Data::EncodedValue& value,
    const Rice::Data::DataSchemaRegistry& schemas = Rice::Data::CompiledDataSchemas());
Rice::Data::EncodedValue FromText(const std::string& type, uint32_t codecVersion, const std::string& text,
    const Rice::Data::DataSchemaRegistry& schemas = Rice::Data::CompiledDataSchemas());
bool UsesJson(const std::string& type, uint32_t codecVersion,
    const Rice::Data::DataSchemaRegistry& schemas = Rice::Data::CompiledDataSchemas());
}
