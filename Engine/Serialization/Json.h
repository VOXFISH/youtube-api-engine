#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Rice::Serialization {
// Deliberately small, bounded JSON implementation for the versioned local schema.
// Settings/protocol numbers remain integers. The explicit data-editor mode preserves
// validated numeric lexemes instead, so float and uint64 values never pass through double.
class Json {
public:
    struct ExactNumber { std::string text; };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Value = std::variant<std::nullptr_t, bool, int64_t, std::string, Array, Object, ExactNumber>;
    Value value = nullptr;
    Json() = default;
    Json(bool v) : value(v) {}
    Json(int v) : value(int64_t(v)) {}
    Json(int64_t v) : value(v) {}
    Json(uint64_t v) : value(int64_t(v)) { if(v > uint64_t(INT64_MAX)) throw std::runtime_error("JSON integer overflow"); }
    Json(const char* v) : value(std::string(v)) {}
    Json(std::string v) : value(std::move(v)) {}
    Json(Array v) : value(std::move(v)) {}
    Json(Object v) : value(std::move(v)) {}
    Json(ExactNumber v) : value(std::move(v)) {}
    const Object& Fields() const { return Get<Object>(); }
    const Array& Items() const { return Get<Array>(); }
    const std::string& String() const { return Get<std::string>(); }
    int64_t Integer() const { return Get<int64_t>(); }
    bool Boolean() const { return Get<bool>(); }
    const Json& At(const std::string& key) const {
        auto& fields = Fields(); auto found = fields.find(key);
        if(found == fields.end()) throw std::runtime_error("Missing JSON field: " + key);
        return found->second;
    }
    const Json* Find(const std::string& key) const {
        auto& fields = Fields(); auto found = fields.find(key);
        return found == fields.end() ? nullptr : &found->second;
    }
    std::string Dump() const {
        if(std::holds_alternative<std::nullptr_t>(value)) return "null";
        if(auto v = std::get_if<bool>(&value)) return *v ? "true" : "false";
        if(auto v = std::get_if<int64_t>(&value)) return std::to_string(*v);
        if(auto v = std::get_if<ExactNumber>(&value)) return v->text;
        if(auto v = std::get_if<std::string>(&value)) return Escape(*v);
        std::string result;
        if(auto v = std::get_if<Array>(&value)) {
            result = "["; for(auto& item : *v) { if(result.size() > 1) result += ','; result += item.Dump(); } return result + ']';
        }
        result = "{"; for(auto& [key, item] : std::get<Object>(value)) {
            if(result.size() > 1) result += ','; result += Escape(key) + ':' + item.Dump();
        }
        return result + '}';
    }
    static Json Parse(std::string_view text) {
        return ParseImpl(text, false);
    }
    static Json ParseExactNumbers(std::string_view text) {
        return ParseImpl(text, true);
    }
private:
    static Json ParseImpl(std::string_view text, bool exactNumbers) {
        if(text.size() > 4 * 1024 * 1024) throw std::runtime_error("JSON document too large");
        Parser parser{text, 0, exactNumbers};
        if(text.size() >= 3 && text.substr(0,3) == "\xef\xbb\xbf") parser.position = 3;
        auto result = parser.Read(0); parser.Space();
        if(parser.position != text.size()) parser.Fail();
        return result;
    }
    template<class T> const T& Get() const {
        if(auto v = std::get_if<T>(&value)) return *v;
        throw std::runtime_error("Incorrect JSON field type");
    }
    static std::string Escape(const std::string& text) {
        constexpr char hex[] = "0123456789abcdef";
        std::string result = "\"";
        for(unsigned char c : text) {
            if(c == '"' || c == '\\') { result += '\\'; result += char(c); }
            else if(c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
            else result += char(c);
        }
        return result + '"';
    }
    struct Parser {
        std::string_view text;
        size_t position = 0;
        bool exactNumbers = false;
        [[noreturn]] void Fail() const { throw std::runtime_error("Invalid JSON at byte " + std::to_string(position)); }
        void Space() { while(position < text.size() && (text[position]==' ' || text[position]=='\n' || text[position]=='\r' || text[position]=='\t')) ++position; }
        bool Take(char c) { Space(); if(position < text.size() && text[position]==c) { ++position; return true; } return false; }
        unsigned Hex4() {
            unsigned result = 0;
            for(int i=0;i<4;++i) {
                if(position == text.size()) Fail(); unsigned char c=text[position++]; unsigned n;
                if(c>='0'&&c<='9') n=c-'0'; else if(c>='a'&&c<='f') n=c-'a'+10; else if(c>='A'&&c<='F') n=c-'A'+10; else Fail();
                result=(result<<4)|n;
            }
            return result;
        }
        static void Utf8Code(std::string& out,unsigned c) {
            if(c<0x80) out+=char(c);
            else if(c<0x800) { out+=char(0xc0|(c>>6));out+=char(0x80|(c&63)); }
            else if(c<0x10000) { out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63)); }
            else { out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63)); }
        }
        std::string String() {
            if(!Take('"')) Fail(); std::string result;
            while(position < text.size()) {
                unsigned char c=text[position++]; if(c=='"') return result; if(c<32) Fail();
                if(c!='\\') { result+=char(c); continue; }
                if(position==text.size()) Fail(); char escaped=text[position++];
                switch(escaped) {
                    case '"':result+='"';break;case '\\':result+='\\';break;case '/':result+='/';break;
                    case 'b':result+='\b';break;case 'f':result+='\f';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
                    case 'u': {
                        auto code=Hex4();
                        if(code>=0xd800&&code<=0xdbff) {
                            if(position+2>text.size()||text.substr(position,2)!="\\u") Fail();position+=2;auto low=Hex4();
                            if(low<0xdc00||low>0xdfff) Fail();code=0x10000+((code-0xd800)<<10)+low-0xdc00;
                        } else if(code>=0xdc00&&code<=0xdfff) Fail();
                        Utf8Code(result,code);break;
                    }
                    default:Fail();
                }
            }
            Fail();
        }
        Json Read(unsigned depth) {
            if(depth>16) throw std::runtime_error("JSON nesting too deep"); Space();if(position==text.size()) Fail();
            if(text[position]=='"') return String();
            if(Take('{')) {
                Object object;if(Take('}')) return object;
                do { auto key=String();if(!Take(':')) Fail();auto item=Read(depth+1);if(!object.emplace(std::move(key),std::move(item)).second) Fail(); } while(Take(','));
                if(!Take('}')) Fail();return object;
            }
            if(Take('[')) { Array array;if(Take(']')) return array;do { array.push_back(Read(depth+1));if(array.size()>10000) Fail(); } while(Take(','));if(!Take(']')) Fail();return array; }
            if(text.substr(position,4)=="true") {position+=4;return true;}
            if(text.substr(position,5)=="false") {position+=5;return false;}
            if(text.substr(position,4)=="null") {position+=4;return {};}
            auto first=position;if(text[position]=='-') ++position;
            if(position==text.size()||text[position]<'0'||text[position]>'9') Fail();
            if(text[position]=='0') ++position;else while(position<text.size()&&text[position]>='0'&&text[position]<='9') ++position;
            if(exactNumbers) {
                if(position<text.size() && text[position]=='.') {
                    ++position;auto digits=position;
                    while(position<text.size()&&text[position]>='0'&&text[position]<='9')++position;
                    if(position==digits)Fail();
                }
                if(position<text.size() && (text[position]=='e'||text[position]=='E')) {
                    ++position;if(position<text.size()&&(text[position]=='+'||text[position]=='-'))++position;
                    auto digits=position;while(position<text.size()&&text[position]>='0'&&text[position]<='9')++position;
                    if(position==digits)Fail();
                }
                return ExactNumber{std::string(text.substr(first,position-first))};
            }
            int64_t number=0;auto parsed=std::from_chars(text.data()+first,text.data()+position,number);
            if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+position) Fail();return number;
        }
    };
};
}
