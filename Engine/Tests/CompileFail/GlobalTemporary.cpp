#include "Engine/Data/DataRoutingMacros.h"
struct InvalidData {
    Rice::Data::DataContext& context;
    RICE_DATA_FIELD_ID(context, int, invalidGlobal, "invalid-global", 0,
        Rice::Data::Scope::Global, Rice::Data::Lifetime::Slot,
        Rice::Data::AccessPolicy::Locked());
};
