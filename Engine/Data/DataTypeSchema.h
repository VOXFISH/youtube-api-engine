#pragma once
#include "Engine/Data/SqliteStore.h"
#include <map>
#include <mutex>
#include <set>

namespace Rice::Data {
struct DataMemberSchema {
    std::string name, type;
    uint32_t codecVersion = 1;
    bool operator==(const DataMemberSchema&) const = default;
};
struct DataTypeSchema {
    std::string type;
    uint32_t codecVersion = 1;
    std::vector<DataMemberSchema> members;
    bool operator==(const DataTypeSchema&) const = default;
};
// Platform-independent metadata. Controller adapters may import explicitly exported schemas;
// an unknown byte blob never acquires a schema by guessing its layout.
class DataSchemaRegistry {
    mutable std::mutex mutex_;
    std::map<std::pair<std::string, uint32_t>, DataTypeSchema> types_;
public:
    void Register(DataTypeSchema schema) {
        if (schema.type.empty() || schema.type.size() > 1024 || !schema.codecVersion || schema.members.size() > 256)
            throw DataError(ErrorCode::InvalidValue, "Invalid data schema metadata");
        std::set<std::string> names;
        for (const auto& member : schema.members) {
            if (member.name.empty() || member.name.size() > 256 || member.type.empty() || member.type.size() > 1024 ||
                !member.codecVersion || !names.insert(member.name).second)
                throw DataError(ErrorCode::InvalidValue, "Invalid or duplicate data schema member");
        }
        std::lock_guard lock(mutex_);
        auto key = std::make_pair(schema.type, schema.codecVersion);
        if (auto found = types_.find(key); found != types_.end()) {
            if (found->second != schema) throw DataError(ErrorCode::SchemaMismatch, "Conflicting exported data schema");
            return;
        }
        types_.emplace(std::move(key), std::move(schema));
    }
    std::optional<DataTypeSchema> Find(const std::string& type, uint32_t version) const {
        std::lock_guard lock(mutex_); auto found = types_.find({type, version});
        return found == types_.end() ? std::optional<DataTypeSchema>{} : found->second;
    }
    std::vector<DataTypeSchema> Export() const {
        std::lock_guard lock(mutex_); std::vector<DataTypeSchema> result; result.reserve(types_.size());
        for (const auto& [key, value] : types_) result.push_back(value);
        return result;
    }
};
inline DataSchemaRegistry& CompiledDataSchemas() { static DataSchemaRegistry registry; return registry; }
} // namespace Rice::Data
