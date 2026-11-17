#pragma once
#include "Engine/Data/DataContext.h"

// Use a type alias for template types containing commas. The explicit ID forms
// keep save identity stable when a source member is renamed.
#define RICE_DATA_FIELD_ID(Context, Type, Name, StableId, Default, ScopeValue, LifetimeValue, Permission) \
    static_assert((ScopeValue) != ::Rice::Data::Scope::Global || (LifetimeValue) == ::Rice::Data::Lifetime::Stored, \
                  #Name " : global variables can't be game(stream) temporary. slot is also forbidden."); \
    ::Rice::Data::Field<Type> Name = (Context).template Bind<Type, (ScopeValue), (LifetimeValue)>( \
        (StableId), (Default), #Name, (Permission))

#define RICE_GAME_FIELD_ID(Context, Type, Name, StableId, Default) \
    RICE_DATA_FIELD_ID(Context, Type, Name, StableId, Default, ::Rice::Data::Scope::Build, ::Rice::Data::Lifetime::Game, ::Rice::Data::AccessPolicy::Locked())
#define RICE_SLOT_FIELD_ID(Context, Type, Name, StableId, Default) \
    RICE_DATA_FIELD_ID(Context, Type, Name, StableId, Default, ::Rice::Data::Scope::Build, ::Rice::Data::Lifetime::Slot, ::Rice::Data::AccessPolicy::Locked())
#define RICE_STORED_FIELD_ID(Context, Type, Name, StableId, Default) \
    RICE_DATA_FIELD_ID(Context, Type, Name, StableId, Default, ::Rice::Data::Scope::Build, ::Rice::Data::Lifetime::Stored, ::Rice::Data::AccessPolicy::Locked())
#define RICE_GLOBAL_FIELD_ID(Context, Type, Name, StableId, Default) \
    RICE_DATA_FIELD_ID(Context, Type, Name, StableId, Default, ::Rice::Data::Scope::Global, ::Rice::Data::Lifetime::Stored, ::Rice::Data::AccessPolicy::Locked())

#define RICE_GAME_FIELD(Context, Type, Name, Default) RICE_GAME_FIELD_ID(Context, Type, Name, #Name, Default)
#define RICE_SLOT_FIELD(Context, Type, Name, Default) RICE_SLOT_FIELD_ID(Context, Type, Name, #Name, Default)
#define RICE_STORED_FIELD(Context, Type, Name, Default) RICE_STORED_FIELD_ID(Context, Type, Name, #Name, Default)
#define RICE_GLOBAL_FIELD(Context, Type, Name, Default) RICE_GLOBAL_FIELD_ID(Context, Type, Name, #Name, Default)
