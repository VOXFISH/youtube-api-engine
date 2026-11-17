#pragma once
#include "Engine/Data/RoutedValue.h"
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>

namespace Rice::Data {
template<class T> struct ValueAccess {
    std::function<T()> read;
    std::function<void(const T&)> write;
    std::function<void(const std::function<void(T&)>&)> edit;
};
template<class T> class ValueProxy;
template<class T> struct ProxyMembers {
    explicit ProxyMembers(std::shared_ptr<ValueAccess<T>>) {}
};
template<class Root, class Member>
std::shared_ptr<ValueAccess<Member>> MemberAccess(std::shared_ptr<ValueAccess<Root>> root, Member Root::* member) {
    auto result = std::make_shared<ValueAccess<Member>>();
    result->read = [root, member] { return root->read().*member; };
    result->write = [root, member](const Member& value) {
        root->edit([&](Root& object) { object.*member = value; });
    };
    result->edit = [root, member](const std::function<void(Member&)>& function) {
        root->edit([&](Root& object) { function(object.*member); });
    };
    return result;
}
template<class Vector, class Scalar>
std::shared_ptr<ValueAccess<Scalar>> ComponentAccess(std::shared_ptr<ValueAccess<Vector>> root, size_t index) {
    auto result = std::make_shared<ValueAccess<Scalar>>();
    result->read = [root, index] { return root->read()[static_cast<glm::length_t>(index)]; };
    result->write = [root, index](const Scalar& value) {
        root->edit([&](Vector& vector) { vector[static_cast<glm::length_t>(index)] = value; });
    };
    result->edit = [root, index](const std::function<void(Scalar&)>& function) {
        root->edit([&](Vector& vector) { function(vector[static_cast<glm::length_t>(index)]); });
    };
    return result;
}
template<class T> concept SequenceValue = requires(T& value) {
    typename T::value_type;
    value.size();
    value.begin();
    value.end();
};
template<SequenceValue T>
std::shared_ptr<ValueAccess<typename T::value_type>> ElementAccess(std::shared_ptr<ValueAccess<T>> root, size_t index) {
    using Element = typename T::value_type;
    auto result = std::make_shared<ValueAccess<Element>>();
    result->read = [root, index] {
        auto value = root->read();
        if (index >= value.size()) throw DataError(ErrorCode::InvalidValue, "Data element index out of range");
        return Element(*std::next(value.begin(), static_cast<typename T::difference_type>(index)));
    };
    result->write = [root, index](const Element& value) {
        root->edit([&](T& container) {
            if (index >= container.size()) throw DataError(ErrorCode::InvalidValue, "Data element index out of range");
            *std::next(container.begin(), static_cast<typename T::difference_type>(index)) = value;
        });
    };
    result->edit = [root, index](const std::function<void(Element&)>& function) {
        root->edit([&](T& container) {
            if (index >= container.size()) throw DataError(ErrorCode::InvalidValue, "Data element index out of range");
            auto iterator = std::next(container.begin(), static_cast<typename T::difference_type>(index));
            Element value = *iterator; function(value); *iterator = std::move(value);
        });
    };
    return result;
}
template<class T> class ValueProxy : public ProxyMembers<T> {
protected:
    std::shared_ptr<ValueAccess<T>> access_;
public:
    explicit ValueProxy(std::shared_ptr<ValueAccess<T>> access) : ProxyMembers<T>(access), access_(std::move(access)) {}
    ValueProxy(const ValueProxy&) = default;
    ValueProxy(ValueProxy&&) = default;
    // Assignment copies the value through the route; it never rebinds the proxy.
    ValueProxy& operator=(const ValueProxy& other) { if (this != &other) Set(other.Get()); return *this; }
    ValueProxy& operator=(ValueProxy&& other) { if (this != &other) Set(other.Get()); return *this; }
    ValueProxy& operator=(const T& value) { Set(value); return *this; }
    T Get() const { return access_->read(); }
    T Read() const { return Get(); }
    operator T() const { return Get(); }
    void Set(const T& value) { access_->write(value); }
    template<class Function> void Edit(Function&& function) { access_->edit(std::forward<Function>(function)); }
    template<class Function> void Mutate(Function&& function) { Edit(std::forward<Function>(function)); }

    ValueProxy& operator+=(T amount) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Edit([amount](T& value) {
            if constexpr (std::is_integral_v<T>) {
                if constexpr (std::is_signed_v<T>) {
                    if ((amount > 0 && value > std::numeric_limits<T>::max() - amount) ||
                        (amount < 0 && value < std::numeric_limits<T>::min() - amount))
                        throw DataError(ErrorCode::InvalidValue, "Data addition overflow");
                } else if (value > std::numeric_limits<T>::max() - amount)
                    throw DataError(ErrorCode::InvalidValue, "Data addition overflow");
            }
            value += amount;
        }); return *this;
    }
    ValueProxy& operator-=(T amount) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Edit([amount](T& value) {
            if constexpr (std::is_integral_v<T>) {
                if constexpr (std::is_signed_v<T>) {
                    if ((amount > 0 && value < std::numeric_limits<T>::min() + amount) ||
                        (amount < 0 && value > std::numeric_limits<T>::max() + amount))
                        throw DataError(ErrorCode::InvalidValue, "Data subtraction overflow");
                } else if (value < amount) throw DataError(ErrorCode::InvalidValue, "Data subtraction overflow");
            }
            value -= amount;
        }); return *this;
    }
    ValueProxy& operator*=(T amount) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Edit([amount](T& value) {
            if constexpr (std::is_integral_v<T>) {
                bool overflow = false;
                if constexpr (std::is_signed_v<T>) {
                    if (value > 0) overflow = amount > 0 ? value > std::numeric_limits<T>::max() / amount
                        : (amount < 0 && amount < std::numeric_limits<T>::min() / value);
                    else if (value < 0) overflow = amount > 0 ? value < std::numeric_limits<T>::min() / amount
                        : (amount < 0 && value < std::numeric_limits<T>::max() / amount);
                } else overflow = amount != 0 && value > std::numeric_limits<T>::max() / amount;
                if (overflow) throw DataError(ErrorCode::InvalidValue, "Data multiplication overflow");
            }
            value *= amount;
        }); return *this;
    }
    ValueProxy& operator/=(T amount) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        Edit([amount](T& value) {
            if (amount == T(0)) throw DataError(ErrorCode::InvalidValue, "Data division by zero");
            if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
                if (value == std::numeric_limits<T>::min() && amount == T(-1))
                    throw DataError(ErrorCode::InvalidValue, "Data division overflow");
            value /= amount;
        }); return *this;
    }
    ValueProxy& operator+=(const std::string& suffix) requires std::is_same_v<T, std::string> {
        Edit([&](T& value) { value += suffix; }); return *this;
    }
    ValueProxy& operator++() requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) { return *this += T(1); }
    ValueProxy& operator--() requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) { return *this -= T(1); }
    T operator++(int) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        T previous{}; Edit([&](T& value) {
            previous = value;
            if constexpr (std::is_integral_v<T>) if (value == std::numeric_limits<T>::max())
                throw DataError(ErrorCode::InvalidValue, "Data increment overflow");
            ++value;
        }); return previous;
    }
    T operator--(int) requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
        T previous{}; Edit([&](T& value) {
            previous = value;
            if constexpr (std::is_integral_v<T>) if (value == std::numeric_limits<T>::lowest())
                throw DataError(ErrorCode::InvalidValue, "Data decrement overflow");
            --value;
        }); return previous;
    }
    size_t size() const requires SequenceValue<T> { return Get().size(); }
    bool empty() const requires SequenceValue<T> { return Get().empty(); }
    auto operator[](size_t index) requires SequenceValue<T> {
        return ValueProxy<typename T::value_type>(ElementAccess(access_, index));
    }
    auto operator[](size_t index) const requires SequenceValue<T> {
        return ElementAccess(access_, index)->read();
    }
    auto at(size_t index) requires SequenceValue<T> { return (*this)[index]; }
    auto at(size_t index) const requires SequenceValue<T> { return (*this)[index]; }
    auto front() requires SequenceValue<T> { return (*this)[0]; }
    auto front() const requires SequenceValue<T> { return (*this)[0]; }
    auto back() requires SequenceValue<T> {
        const auto count = size();
        if (!count) throw DataError(ErrorCode::InvalidValue, "Empty data list has no back element");
        return (*this)[count - 1];
    }
    void clear() requires requires(T& value) { value.clear(); } { Edit([](T& value) { value.clear(); }); }
    template<class Element> void push_back(Element&& element) requires requires(T& value, Element&& item) { value.push_back(std::forward<Element>(item)); } {
        typename T::value_type copy(std::forward<Element>(element));
        Edit([&](T& value) { value.push_back(copy); });
    }
    template<class... Args> void emplace_back(Args&&... arguments) requires requires(T& value) { value.emplace_back(std::forward<Args>(arguments)...); } {
        typename T::value_type copy(std::forward<Args>(arguments)...);
        Edit([&](T& value) { value.push_back(copy); });
    }
    void pop_back() requires requires(T& value) { value.pop_back(); } {
        Edit([](T& value) {
            if (value.empty()) throw DataError(ErrorCode::InvalidValue, "Cannot pop an empty data list");
            value.pop_back();
        });
    }
    void resize(size_t size) requires requires(T& value) { value.resize(size); } {
        if (size > CodecDetail::MaximumElements) throw DataError(ErrorCode::InvalidValue, "Data list exceeds limit");
        Edit([size](T& value) { value.resize(size); });
    }
    void erase(size_t index) requires requires(T& value) { value.erase(value.begin()); } {
        Edit([index](T& value) {
            if (index >= value.size()) throw DataError(ErrorCode::InvalidValue, "Data erase index out of range");
            value.erase(std::next(value.begin(), static_cast<typename T::difference_type>(index)));
        });
    }
    template<class Element> void insert(size_t index, Element&& item) requires requires(T& value, const typename T::value_type& element) { value.insert(value.begin(), element); } {
        typename T::value_type copy(std::forward<Element>(item));
        Edit([index, &copy](T& value) {
            if (index > value.size()) throw DataError(ErrorCode::InvalidValue, "Data insert index out of range");
            value.insert(std::next(value.begin(), static_cast<typename T::difference_type>(index)), copy);
        });
    }
};

// Named GLM members use proxies; no mutable pointer/reference escapes.
template<class T, glm::qualifier Q> struct ProxyMembers<glm::vec<2, T, Q>> {
    ValueProxy<T> x, y;
    explicit ProxyMembers(std::shared_ptr<ValueAccess<glm::vec<2, T, Q>>> root)
        : x(ComponentAccess<glm::vec<2, T, Q>, T>(root, 0)), y(ComponentAccess<glm::vec<2, T, Q>, T>(root, 1)) {}
};
template<class T, glm::qualifier Q> struct ProxyMembers<glm::vec<3, T, Q>> {
    ValueProxy<T> x, y, z;
    explicit ProxyMembers(std::shared_ptr<ValueAccess<glm::vec<3, T, Q>>> root)
        : x(ComponentAccess<glm::vec<3, T, Q>, T>(root, 0)), y(ComponentAccess<glm::vec<3, T, Q>, T>(root, 1)),
          z(ComponentAccess<glm::vec<3, T, Q>, T>(root, 2)) {}
};
template<Encodable T> class Field : public ValueProxy<T> {
    struct State {
        T defaults;
        std::string description;
        std::shared_ptr<Routed<T>> route;
        explicit State(T value) : defaults(std::move(value)) {}
    };
    std::shared_ptr<State> state_;
    static std::shared_ptr<ValueAccess<T>> Access(const std::shared_ptr<State>& state) {
        auto access = std::make_shared<ValueAccess<T>>();
        access->read = [state] { return state->route ? state->route->Get() : state->defaults; };
        access->write = [state](const T& value) {
            if (state->route) state->route->Set(value); else { EncodeValue(value); state->defaults = value; }
        };
        access->edit = [state](const std::function<void(T&)>& function) {
            if (state->route) state->route->Edit(function);
            else { T copy = state->defaults; function(copy); EncodeValue(copy); state->defaults = std::move(copy); }
        };
        return access;
    }
    explicit Field(std::shared_ptr<State> state) : ValueProxy<T>(Access(state)), state_(std::move(state)) {}
public:
    Field(T defaults = T{}) : Field(std::make_shared<State>(std::move(defaults))) { EncodeValue(state_->defaults); }
    explicit Field(Routed<T> binding) : Field(DecodeValue<T>(binding.DefaultRecord().value)) {
        state_->route = std::make_shared<Routed<T>>(std::move(binding));
    }
    using ValueProxy<T>::operator=;
    // A copied field is another handle to the same route; copying does not duplicate database identity.
    Field(const Field&) = default;
    Field(Field&&) = default;
    Field& operator=(const Field& other) { if (this != &other) this->Set(other.Get()); return *this; }
    Field& operator=(Field&& other) { if (this != &other) this->Set(other.Get()); return *this; }
    void Bind(IDataStore& store, RouteKey key, std::string name = {}, std::shared_ptr<SlotContext> slot = {}) {
        state_->route = std::make_shared<Routed<T>>(store, std::move(key), state_->defaults, std::move(name), state_->description, std::move(slot));
    }
    void Bind(GlobalFallback& global, RouteKey key, std::string name = {}) {
        state_->route = std::make_shared<Routed<T>>(global, std::move(key), state_->defaults, std::move(name), state_->description);
    }
    Field& Description(std::string description) {
        if (description.size() > 65536) throw DataError(ErrorCode::InvalidValue, "Data description exceeds limit");
        state_->description = std::move(description);
        if (state_->route) state_->route->Description(state_->description);
        return *this;
    }
    DataRecord DefaultRecord() const {
        if (!state_->route) throw DataError(ErrorCode::InvalidKey, "Field has not been bound");
        return state_->route->DefaultRecord();
    }
};
} // namespace Rice::Data

// Call at global namespace after defining the native struct and an X-macro member list.
// The list accepts X(MemberTypeAlias, memberName); aliases avoid commas inside macro type arguments.
#define RICE_DETAIL_CODEC_WRITE(MemberType, MemberName) ::Rice::Data::DataCodec<MemberType>::Write(writer, value.MemberName);
#define RICE_DETAIL_CODEC_READ(MemberType, MemberName) value.MemberName = ::Rice::Data::DataCodec<MemberType>::Read(reader);
#define RICE_DETAIL_CODEC_ASSERT(MemberType, MemberName) static_assert(std::same_as<MemberType, std::remove_cvref_t<decltype(std::declval<ValueType&>().MemberName)>>, "Registered data member type does not match the native struct");
#define RICE_DETAIL_SCHEMA_MEMBER(MemberType, MemberName) {#MemberName, ::Rice::Data::DataCodec<MemberType>::Type(), ::Rice::Data::DataCodec<MemberType>::Version},
#define RICE_DETAIL_SCHEMA_REGISTER(MemberType, MemberName) ::Rice::Data::RegisterDataSchema<MemberType>();
#define RICE_DETAIL_PROXY_DECLARE(MemberType, MemberName) ::Rice::Data::ValueProxy<MemberType> MemberName;
#define RICE_DETAIL_PROXY_INITIALIZE(MemberType, MemberName) , MemberName(::Rice::Data::MemberAccess(root, &ValueType::MemberName))
#define RICE_DATA_STRUCT(TypeName, StableTypeId, CodecVersion, Fields) \
template<> struct Rice::Data::DataCodec<TypeName> { \
    using ValueType = TypeName; \
    static constexpr uint32_t Version = CodecVersion; \
    static_assert(Version > 0, "A data codec requires a positive version"); \
    Fields(RICE_DETAIL_CODEC_ASSERT) \
    static ::Rice::Data::DataTypeSchema Schema() { return {Type(), Version, { Fields(RICE_DETAIL_SCHEMA_MEMBER) }}; } \
    static void RegisterMembers() { Fields(RICE_DETAIL_SCHEMA_REGISTER) } \
    static std::string Type() { return StableTypeId; } \
    static void Write(::Rice::Data::CodecDetail::Writer& writer, const TypeName& value) { Fields(RICE_DETAIL_CODEC_WRITE) } \
    static TypeName Read(::Rice::Data::CodecDetail::Reader& reader) { TypeName value{}; Fields(RICE_DETAIL_CODEC_READ) return value; } \
}; \
template<> struct Rice::Data::ProxyMembers<TypeName> { \
    using ValueType = TypeName; \
    std::shared_ptr<::Rice::Data::ValueAccess<TypeName>> root_; \
    Fields(RICE_DETAIL_PROXY_DECLARE) \
    explicit ProxyMembers(std::shared_ptr<::Rice::Data::ValueAccess<TypeName>> root) \
        : root_(root) Fields(RICE_DETAIL_PROXY_INITIALIZE) {} \
};
