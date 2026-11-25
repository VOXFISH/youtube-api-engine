#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dpapi.h>
#include <objbase.h>
#include "CredentialStore.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace RiceController::Streaming {
namespace {
void ValidateRef(std::string_view value) {
    if(value.empty() || value.size()>80 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string_view::npos)
        throw std::runtime_error("Invalid credential reference");
}
DATA_BLOB Blob(std::string_view value) { return {static_cast<DWORD>(value.size()),reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))}; }
struct ProtectedBytes {
    DATA_BLOB value{};
    ~ProtectedBytes(){if(value.pbData){SecureZeroMemory(value.pbData,value.cbData);LocalFree(value.pbData);}}
};
std::wstring Suffix() {
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create credential transaction ID");
    wchar_t text[40]{};StringFromGUID2(id,text,40);return text;
}
}
CredentialStore::CredentialStore(std::filesystem::path directory):directory_(std::filesystem::absolute(std::move(directory))) {
    std::filesystem::create_directories(directory_);
}
std::filesystem::path CredentialStore::File(std::string_view reference)const {
    ValidateRef(reference);return directory_/(std::string(reference)+".dpapi");
}
void CredentialStore::Put(std::string_view reference,std::string_view secret) {
    const auto file=File(reference);
    if(secret.empty() || secret.size()>65536)throw std::runtime_error("Credential must contain 1 to 65536 bytes");
    const auto purpose=std::string("RiceController.Credential.v1:")+std::string(reference);
    auto plain=Blob(secret),entropy=Blob(purpose);ProtectedBytes encrypted;
    if(!CryptProtectData(&plain,L"Rice Controller credential",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.value))
        throw std::runtime_error("Cannot protect credential for the current Windows user");
    auto temporary=file;temporary+=L"."+Suffix()+L".tmp";
    try {
        {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write("RCDPAPI1",8);
         stream.write(reinterpret_cast<const char*>(encrypted.value.pbData),encrypted.value.cbData);stream.flush();
         if(!stream)throw std::runtime_error("Cannot write encrypted credential");}
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot commit encrypted credential");
    }catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
std::optional<std::string> CredentialStore::Get(std::string_view reference)const {
    const auto file=File(reference);if(!std::filesystem::exists(file))return {};
    const auto size=std::filesystem::file_size(file);if(size<9 || size>131072)throw std::runtime_error("Invalid encrypted credential size");
    std::string bytes(static_cast<size_t>(size),'\0');std::ifstream stream(file,std::ios::binary);stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!stream || bytes.substr(0,8)!="RCDPAPI1")throw std::runtime_error("Invalid encrypted credential file");
    const auto purpose=std::string("RiceController.Credential.v1:")+std::string(reference);
    auto encrypted=Blob(std::string_view(bytes).substr(8)),entropy=Blob(purpose);ProtectedBytes decrypted;
    if(!CryptUnprotectData(&encrypted,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&decrypted.value))
        throw std::runtime_error("Cannot decrypt credential for the current Windows user and reference");
    if(!decrypted.value.cbData || decrypted.value.cbData>65536)throw std::runtime_error("Invalid decrypted credential size");
    return std::string(reinterpret_cast<const char*>(decrypted.value.pbData),decrypted.value.cbData);
}
bool CredentialStore::Erase(std::string_view reference) { return std::filesystem::remove(File(reference)); }
std::string Redact(std::string text,const std::vector<std::string>& secrets) {
    for(const auto& secret:secrets)if(!secret.empty()) {
        size_t at=0;while((at=text.find(secret,at))!=std::string::npos){text.replace(at,secret.size(),"[redacted]");at+=10;}
    }
    std::string folded=text;std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    for(const auto* marker:{"key=","access_token=","refresh_token=","client_secret=","authorization: bearer ","x-goog-api-key: "}) {
        size_t from=0;
        while((from=folded.find(marker,from))!=std::string::npos) {
            auto start=from+std::char_traits<char>::length(marker),end=start;
            while(end<text.size() && text[end]!='&' && text[end]!=' ' && text[end]!='\r' && text[end]!='\n' && text[end]!='"' && text[end]!='\'')++end;
            text.replace(start,end-start,"[redacted]");folded.replace(start,end-start,"[redacted]");from=start+10;
        }
    }
    return text;
}
}
