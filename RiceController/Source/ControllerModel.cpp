#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#include "ControllerModel.h"
#include "../Shared/Json.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace RiceController {
namespace fs = std::filesystem;
std::wstring Wide(const std::string& text) {
    if(text.empty())return {};
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!size)throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);return result;
}
std::string Utf8(const std::wstring& text) {
    if(text.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("Invalid Unicode text");
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr);return result;
}
std::string PathText(const fs::path& path){return Utf8(path.native());}
fs::path TextPath(const std::string& text){auto wide=Wide(text);if(wide.find(L'\0')!=std::wstring::npos)throw std::runtime_error("Path contains a NUL character");return fs::path(wide);}
std::string NewId(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create instance ID");wchar_t buffer[40]{};StringFromGUID2(guid,buffer,40);return Utf8(std::wstring(buffer+1,36));}
static bool IsSpace(wchar_t c){WORD kind=0;return GetStringTypeW(CT_CTYPE1,&c,1,&kind)&&(kind&C1_SPACE);}
void ValidateName(std::string& name){
    auto text=Wide(name);
    while(!text.empty()&&IsSpace(text.front()))text.erase(text.begin());
    while(!text.empty()&&IsSpace(text.back()))text.pop_back();
    size_t characters=0;
    for(size_t i=0;i<text.size();++i){
        auto c=text[i];if(c<32||(c>=127&&c<=159))throw std::runtime_error("이름에는 제어 문자를 사용할 수 없습니다.");
        if(c>=0xd800&&c<=0xdbff)++i;++characters;
    }
    if(!characters||characters>64)throw std::runtime_error("이름은 1~64자로 지정해 주세요.");
    name=Utf8(text);
}
std::string DefaultName(const std::vector<Entry>& entries){
    std::set<std::string> names;
    for(auto& entry:entries){auto name=entry.name;try{ValidateName(name);}catch(...){}names.insert(name);}
    for(size_t n=1;;++n){auto candidate=std::string("빌드 ")+std::to_string(n);if(!names.contains(candidate))return candidate;}
}
static bool ValidMode(const std::string& mode){return mode=="windowed"||mode=="hidden"||mode=="fullscreen";}
static void ValidateShape(Entry& entry){
    if(entry.id.empty()||entry.id.size()>80||entry.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
        throw std::runtime_error("Invalid entry ID");
    ValidateName(entry.name);
    if(entry.executable.empty()||entry.project.empty())throw std::runtime_error("실행 파일과 프로젝트 위치를 지정해 주세요.");
    if(!ValidMode(entry.windowMode))throw std::runtime_error("Invalid window mode");
    entry.executable=fs::absolute(entry.executable).lexically_normal();entry.project=fs::absolute(entry.project).lexically_normal();
}
void ValidateEntry(Entry& entry){
    if(entry.id.empty())entry.id=NewId();
    ValidateShape(entry);
    if(fs::is_directory(entry.executable))entry.executable/="RicePlayer.exe";
    auto extension=entry.executable.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
    if(extension!=L".exe"||!fs::is_regular_file(entry.executable))throw std::runtime_error("실행 가능한 빌드 파일을 찾을 수 없습니다.");
    if(fs::is_regular_file(entry.project)){
        if(entry.project.filename()!=L"project.rice")throw std::runtime_error("project.rice 또는 프로젝트 폴더를 선택해 주세요.");
        entry.project=entry.project.parent_path();
    }
    if(!fs::is_directory(entry.project)||!fs::is_regular_file(entry.project/"project.rice")||!fs::is_directory(entry.project/"Assets"))
        throw std::runtime_error("project.rice와 Assets가 포함된 완성된 프로젝트를 선택해 주세요.");
}
std::vector<Entry> LoadEntries(const fs::path& path){
    if(!fs::exists(path))return {};
    if(fs::file_size(path)>4*1024*1024)throw std::runtime_error("Controller settings file is too large");
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read controller settings");
    std::ostringstream contents;contents<<stream.rdbuf();if(stream.bad())throw std::runtime_error("Cannot read controller settings");
    auto json=Json::Parse(contents.str());if(json.At("schemaVersion").Integer()!=1)throw std::runtime_error("Unsupported controller settings version");
    auto& items=json.At("entries").Items();if(items.size()>1000)throw std::runtime_error("Too many controller entries");
    std::vector<Entry> entries;std::set<std::string> ids;
    for(auto& item:items){
        Entry entry{item.At("entryId").String(),item.At("name").String(),TextPath(item.At("executablePath").String()),TextPath(item.At("projectPath").String()),item.At("launchWindowMode").String()};
        ValidateShape(entry);if(!ids.insert(entry.id).second)throw std::runtime_error("Duplicate controller entry ID");entries.push_back(std::move(entry));
    }
    return entries;
}
void SaveEntries(const fs::path& path,const std::vector<Entry>& entries){
    if(entries.size()>1000)throw std::runtime_error("Too many controller entries");
    Json::Array items;std::set<std::string> ids;
    for(auto entry:entries){
        ValidateShape(entry);if(!ids.insert(entry.id).second)throw std::runtime_error("Duplicate controller entry ID");
        items.emplace_back(Json::Object{{"entryId",entry.id},{"name",entry.name},{"executablePath",PathText(entry.executable)},
            {"projectPath",PathText(entry.project)},{"launchWindowMode",entry.windowMode}});
    }
    auto contents=Json(Json::Object{{"schemaVersion",1},{"entries",std::move(items)}}).Dump()+"\n";
    auto destination=fs::absolute(path);if(!destination.parent_path().empty())fs::create_directories(destination.parent_path());
    auto temp=destination;temp+=L"."+Wide(NewId())+L".tmp";
    try {
        {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);stream.write(contents.data(),static_cast<std::streamsize>(contents.size()));stream.flush();if(!stream)throw std::runtime_error("Cannot write controller settings");}
        if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace controller settings (Win32 "+std::to_string(GetLastError())+")");
    }catch(...){std::error_code error;fs::remove(temp,error);throw;}
}
fs::path DefaultConfigPath(){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)))throw std::runtime_error("Cannot resolve local app data directory");fs::path result=path;CoTaskMemFree(path);return result/"RiceController"/"controller.json";}
std::wstring QuoteArgument(const std::wstring& value){
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;slashes=0;continue;}result.append(slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');result+=L'\"';return result;
}
}
