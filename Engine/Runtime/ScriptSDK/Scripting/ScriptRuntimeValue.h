#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Rice::Scripting {
struct Vec2 { float x{}, y{}; bool operator==(const Vec2&) const = default; };
struct Vec3 { float x{}, y{}, z{}; bool operator==(const Vec3&) const = default; };
inline Vec2 operator+(Vec2 a,Vec2 b){return {a.x+b.x,a.y+b.y};}
inline Vec2 operator-(Vec2 a,Vec2 b){return {a.x-b.x,a.y-b.y};}
inline Vec2 operator*(Vec2 a,float n){return {a.x*n,a.y*n};}
inline Vec2 operator*(float n,Vec2 a){return a*n;}
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float n){return {a.x*n,a.y*n,a.z*n};}
inline Vec3 operator*(float n,Vec3 a){return a*n;}

enum class ValueKind : int { None, Int, Float, Bool, Vector2, Vector3, Encoded };
// Compound data uses the Data Routing wire format. The caller consumes a
// borrowed result before its producer's next compound conversion on this thread.
struct Value {
    ValueKind kind{};
    double x{}, y{}, z{};
    const char* type{};
    const std::uint8_t* bytes{};
    std::size_t length{};
};
inline Value ToValue(int n) { return {ValueKind::Int, static_cast<double>(n)}; }
inline Value ToValue(float n) { return {ValueKind::Float, n}; }
inline Value ToValue(double n) { return {ValueKind::Float, n}; }
inline Value ToValue(bool n) { return {ValueKind::Bool, n ? 1.0 : 0.0}; }
inline Value ToValue(Vec2 v) { return {ValueKind::Vector2, v.x, v.y}; }
inline Value ToValue(Vec3 v) { return {ValueKind::Vector3, v.x, v.y, v.z}; }

inline int CheckedInt(double v) {
    if (!std::isfinite(v) || std::trunc(v) < std::numeric_limits<int>::min() ||
        std::trunc(v) > std::numeric_limits<int>::max())
        throw std::range_error("This number is outside the integer range.");
    return static_cast<int>(v);
}
namespace Wire {
inline constexpr std::size_t MaxBytes = 16*1024*1024, MaxElements = 1024*1024;
struct Writer {
    std::vector<std::uint8_t> bytes;
    void Byte(std::uint8_t b){
        if(bytes.size()>=MaxBytes)throw std::length_error("Script value exceeds storage limit.");
        bytes.push_back(b);
    }
    template<class U> void Integer(U n) {
        for(std::size_t i=0;i<sizeof(U);++i)Byte(static_cast<std::uint8_t>(n>>(8*i)));
    }
    void Count(std::size_t n) {
        if(n>MaxElements)throw std::length_error("Script container exceeds element limit.");
        Integer(std::uint64_t(n));
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::uint8_t Byte() {
        if(bytes.empty())throw std::runtime_error("Truncated script value.");
        auto b=bytes.front();bytes=bytes.subspan(1);return b;
    }
    template<class U> U Integer() {
        U n{};
        for(std::size_t i=0;i<sizeof(U);++i)n|=static_cast<U>(Byte())<<(8*i);
        return n;
    }
    std::size_t Count() {
        auto size=Integer<std::uint64_t>();
        if(size>MaxElements)throw std::length_error("Invalid script container length.");
        return static_cast<std::size_t>(size);
    }
};

template<class T> struct Codec;
template<> struct Codec<int> {
    static std::string Tag(){return "i32";}
    static void Write(Writer& w,int n){w.Integer(std::bit_cast<std::uint32_t>(n));}
    static int Read(Reader& r){return std::bit_cast<int>(r.Integer<std::uint32_t>());}
};
template<> struct Codec<bool>{
    static std::string Tag(){return "bool";}
    static void Write(Writer& w,bool n){w.Byte(n?1:0);}
    static bool Read(Reader& r){auto b=r.Byte();if(b>1)throw std::runtime_error("Invalid boolean.");return b!=0;}
};
template<class T> requires std::is_floating_point_v<T> struct Codec<T> {
    using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
    static std::string Tag(){return sizeof(T)==4?"f32":"f64";}
    static T Finite(T n){if(!std::isfinite(n))throw std::runtime_error("Cannot store a nonfinite number.");return n;}
    static void Write(Writer& w,T n){w.Integer(std::bit_cast<U>(Finite(n)));}
    static T Read(Reader& r){return Finite(std::bit_cast<T>(r.Integer<U>()));}
};
template<> struct Codec<Vec2>{
    static std::string Tag(){return "vec2(f32)";}
    static void Write(Writer& w,Vec2 v){Codec<float>::Write(w,v.x);Codec<float>::Write(w,v.y);}
    static Vec2 Read(Reader& r){return {Codec<float>::Read(r),Codec<float>::Read(r)};}
};
template<> struct Codec<Vec3>{
    static std::string Tag(){return "vec3(f32)";}
    static void Write(Writer& w,Vec3 v){Codec<float>::Write(w,v.x);Codec<float>::Write(w,v.y);Codec<float>::Write(w,v.z);}
    static Vec3 Read(Reader& r){return {Codec<float>::Read(r),Codec<float>::Read(r),Codec<float>::Read(r)};}
};
template<> struct Codec<std::string>{
    static std::string Tag(){return "utf8";}
    static void Write(Writer& w,const std::string& v){w.Count(v.size());for(unsigned char c:v)w.Byte(c);}
    static std::string Read(Reader& r){
        auto size=r.Count();if(size>r.bytes.size())throw std::runtime_error("Truncated script string.");
        std::string result;result.reserve(size);while(size--)result.push_back(static_cast<char>(r.Byte()));return result;
    }
};
template<class T> struct Codec<std::vector<T>> {
    static std::string Tag(){return "list("+Codec<T>::Tag()+":1)";}
    static void Write(Writer& w,const std::vector<T>& v){w.Count(v.size());for(const auto& x:v)Codec<T>::Write(w,x);}
    static std::vector<T> Read(Reader& r){
        auto size=r.Count();std::vector<T> result;
        while(size--)result.push_back(Codec<T>::Read(r));
        return result;
    }
};
template<class K,class V> struct Codec<std::unordered_map<K,V>> {
    using Map=std::unordered_map<K,V>;
    static std::string Tag(){return "dictionary("+Codec<K>::Tag()+":1,"+Codec<V>::Tag()+":1)";}
    static void Write(Writer& w,const Map& v){
        w.Count(v.size());
        std::vector<const typename Map::value_type*> rows;
        rows.reserve(v.size());for(const auto& row:v)rows.push_back(&row);
        std::sort(rows.begin(),rows.end(),[](auto a,auto b){return a->first<b->first;});
        for(auto row:rows){Codec<K>::Write(w,row->first);Codec<V>::Write(w,row->second);}
    }
    static Map Read(Reader& r){
        auto size=r.Count();Map result;
        while(size--){
            auto key=Codec<K>::Read(r);
            if(!result.emplace(std::move(key),Codec<V>::Read(r)).second)
                throw std::runtime_error("Duplicate script dictionary key.");
        }
        return result;
    }
};
// Author-defined data classes explicitly list the value members that form
// their schema. Class padding, object addresses and lifecycle hooks are never encoded.
template<class T> requires requires(T& t,const T& ct){T::RiceType;t.RiceFields();ct.RiceFields();}
struct Codec<T> {
    using Fields=decltype(std::declval<T&>().RiceFields());
    static std::string Tag(){
        std::string result="script-class("+std::string(T::RiceType);
        [&]<std::size_t... I>(std::index_sequence<I...>){
            ((result+=","+Codec<std::remove_cvref_t<std::tuple_element_t<I,Fields>>>::Tag()+":1"),...);
        }(std::make_index_sequence<std::tuple_size_v<Fields>>{});
        return result+")";
    }
    static void Write(Writer& w,const T& object){
        std::apply([&](const auto&... x){(Codec<std::remove_cvref_t<decltype(x)>>::Write(w,x),...);},object.RiceFields());
    }
    static T Read(Reader& r){
        T result{};
        std::apply([&](auto&... x){((x=Codec<std::remove_cvref_t<decltype(x)>>::Read(r)),...);},result.RiceFields());
        return result;
    }
};
}

inline Value BorrowEncoded(std::string type,std::vector<std::uint8_t> bytes) {
    if(type.empty()||type.size()>1024||bytes.size()>Wire::MaxBytes)throw std::runtime_error("Invalid encoded script value.");
    static thread_local std::string tag;
    static thread_local std::vector<std::uint8_t> buffer;
    tag=std::move(type);buffer=std::move(bytes);
    return {ValueKind::Encoded,0,0,0,tag.c_str(),buffer.data(),buffer.size()};
}
template<class T> requires requires { Wire::Codec<T>::Tag(); }
Value ToValue(const T& item) {
    Wire::Writer writer;Wire::Codec<T>::Write(writer,item);
    return BorrowEncoded(Wire::Codec<T>::Tag(),std::move(writer.bytes));
}
template<class T> T NumberValue(Value v) {
    if constexpr (std::is_same_v<T,int>) {
        if(v.kind==ValueKind::Int||v.kind==ValueKind::Float)return CheckedInt(v.x);
    } else if constexpr(std::is_floating_point_v<T>) {
        if(v.kind==ValueKind::Int||v.kind==ValueKind::Float)return static_cast<T>(v.x);
    } else if constexpr(std::is_same_v<T,bool>) {
        if(v.kind==ValueKind::Bool)return v.x!=0;
    } else if constexpr(std::is_same_v<T,Vec2>) {
        if(v.kind==ValueKind::Vector2)return {float(v.x),float(v.y)};
    } else if constexpr(std::is_same_v<T,Vec3>) {
        if(v.kind==ValueKind::Vector3)return {float(v.x),float(v.y),float(v.z)};
    } else if constexpr(requires{Wire::Codec<T>::Tag();}) {
        if(v.kind==ValueKind::Encoded&&v.type&&Wire::Codec<T>::Tag()==v.type&&v.length<=Wire::MaxBytes&&(!v.length||v.bytes)){
            Wire::Reader reader{std::span(v.bytes,v.length)};
            T result=Wire::Codec<T>::Read(reader);
            if(!reader.bytes.empty())throw std::runtime_error("Unexpected bytes after script value.");
            return result;
        }
    }
    throw std::runtime_error("This value does not match the variable type.");
}
}
