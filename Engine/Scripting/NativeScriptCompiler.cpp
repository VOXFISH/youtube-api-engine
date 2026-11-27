#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "NativeScriptCompiler.h"
#include "ScriptAuthoring.h"

namespace Rice::Scripting {
namespace fs = std::filesystem;
namespace {
std::string Utf8(const fs::path& path) {
    auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
std::string UniqueName() {
    static std::atomic<unsigned long> sequence{};
    std::ostringstream out;
    out << std::hex << GetCurrentProcessId() << '-' << std::chrono::steady_clock::now().time_since_epoch().count()
        << '-' << sequence.fetch_add(1);
    return out.str();
}
std::string Bucket(const std::string& key) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : key) { hash ^= c; hash *= 1099511628211ULL; }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}
std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + Utf8(path));
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
void Write(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    if (!out) throw std::runtime_error("Cannot write " + Utf8(path));
}
void Replace(const fs::path& from, const fs::path& to) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish " + Utf8(to) + " (Windows error " + std::to_string(GetLastError()) + ").");
}
struct TemporaryFile {
    fs::path path;
    ~TemporaryFile() { std::error_code ignored; fs::remove(path, ignored); }
};
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

std::wstring Quote(const std::wstring& word) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t c : word) {
        if (c == L'\\') { ++slashes; continue; }
        quoted.append(c == L'"' ? 2*slashes+1 : slashes, L'\\');
        quoted += c;
        slashes = 0;
    }
    quoted.append(slashes*2, L'\\');
    return quoted + L"\"";
}
void RunCMake(const fs::path& work, const std::vector<std::wstring>& args) {
    fs::path cmake = RICE_SCRIPT_CMAKE;
    if (!fs::is_regular_file(cmake)) cmake = L"cmake.exe";
    std::wstring command = Quote(cmake.wstring());
    for (auto& arg : args) command += L" " + Quote(arg);
    const fs::path log = work / "compile.log";
    DWORD status = 1;
    {
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle output{CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.h == INVALID_HANDLE_VALUE || input.h == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open the script compiler log.");
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = input.h;
        si.hStdOutput = si.hStdError = output.h;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(cmake.is_absolute() ? cmake.c_str() : nullptr, command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, work.c_str(), &si, &pi))
            throw std::runtime_error("Cannot start CMake. Install CMake and the C++ build tools to save a script.");
        Handle process{pi.hProcess}, thread{pi.hThread};
        if (WaitForSingleObject(process.h, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(process.h, &status))
            throw std::runtime_error("Cannot read script compiler status.");
    }
    if (status) throw std::runtime_error("C++ script compilation failed.\n" + Read(log));
}
fs::path ModuleProject() {
    std::wstring executable(32768, L'\0');
    DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    executable.resize(count);
    auto base = fs::path(executable).parent_path();
    auto tree = base / "Engine" / "Scripting" / "ModuleProject";
    if (fs::is_regular_file(tree / "CMakeLists.txt")) return tree;
    auto installed = base / "ScriptSDK" / "Scripting" / "ModuleProject";
    if (fs::is_regular_file(installed / "CMakeLists.txt")) return installed;
    return RICE_SCRIPT_MODULE_PROJECT;
}
struct Manifest { std::string key, binary; };
Manifest ReadManifest(const fs::path& folder) {
    Manifest result;
    std::ifstream in(folder / "current");
    if (!(in >> std::quoted(result.key) >> result.binary) || folder.filename() != Bucket(result.key) ||
        !result.binary.ends_with(".dll") || result.binary.find_first_not_of("0123456789abcdef-.l") != std::string::npos)
        throw std::runtime_error("Invalid saved script module in " + Utf8(folder));
    return result;
}
std::pair<Manifest,PublishedModule> PinPublication(const fs::path& folder) {
    for (int attempt=0; attempt<3; ++attempt) {
        auto manifest=ReadManifest(folder);
        auto path=folder / manifest.binary;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                                nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file!=INVALID_HANDLE_VALUE)
            return {std::move(manifest),{path,std::shared_ptr<void>(file,[](void* handle){ CloseHandle(handle); })}};
        if (GetLastError()!=ERROR_FILE_NOT_FOUND) break;
    }
    throw std::runtime_error("Cannot open the published native script in " + Utf8(folder));
}
void RetireOlderBuilds(const fs::path& folder, const fs::path& current) {
    std::error_code error;
    for (fs::directory_iterator it(folder,error),end; !error && it!=end; it.increment(error)) {
        auto path=it->path();
        if (path==current || path.extension()!=".dll") continue;
        auto stem=path.stem().wstring();
        if (std::count(stem.begin(),stem.end(),L'-')!=2 || stem.find_first_not_of(L"0123456789abcdef-")!=std::wstring::npos) continue;
        // Windows refuses to remove a build pinned by a running Play or export.
        std::error_code ignored;
        if (fs::remove(path,ignored)) fs::remove(path.replace_extension(".cpp"),ignored);
    }
}
std::string NativeTranslationUnit(const std::string& source,const std::string& scriptKey) {
    ScriptDocument document;
    document.SetSource(source);
    auto unit=document.InstrumentedSource(scriptKey);
    if (document.HasModuleEntry()) return unit;
    unit+="\n#include \"Scripting/ScriptRuntime.h\"\n";
    if (document.HasScriptBody()) {
        unit += "RICE_SCRIPT_MODULE("+document.ScriptClassName()+")\n";
    } else {
        // A helper-only C++ file has no lifecycle callbacks. Free native hooks
        // get a component adapter without inserting boilerplate into the file.
        unit += "namespace { struct RiceImportedScript : Rice::Scripting::ScriptBase {\n";
        if (document.FreeHookArity("Start")==0) unit += "void Start() { ::Start(); }\n";
        int update=document.FreeHookArity("Update");
        if (update==0) unit += "void Update() { ::Update(); }\n";
        if (update==1) unit += "void Update(float dt) { ::Update(dt); }\n";
        unit += "}; }\nRICE_SCRIPT_MODULE(RiceImportedScript)\n";
    }
    return unit;
}
}

fs::path ScriptSourcePath(const fs::path& project,const fs::path& link) {
    if(link.empty())throw std::invalid_argument("Choose a C++ script file.");
    if(link.is_absolute())return link.lexically_normal();
    auto relative=link.lexically_normal();
    auto source=*relative.begin()=="Scripts"?project/relative:project/"Scripts"/relative;
    auto legacy=project/"Assets"/relative;
    if(!fs::is_regular_file(source)&&fs::is_regular_file(legacy))source=legacy;
    return fs::absolute(source).lexically_normal();
}
std::vector<fs::path> ScriptFiles(const fs::path& project) {
    std::vector<fs::path> result;
    for(const auto& directory:{project/"Scripts",project/"Assets"}){
        if(!fs::is_directory(directory))continue;
        for(const auto& entry:fs::recursive_directory_iterator(directory)){
            auto ext=entry.path().extension();
            if(entry.is_regular_file()&&(ext==".cpp"||ext==".h"||ext==".hpp"||ext==".cxx"||ext==".cc"))
                result.push_back(entry.path());
        }
    }
    std::sort(result.begin(),result.end());
    return result;
}
fs::path ScriptModuleDirectory(const fs::path& project) {
    auto directory=project/"Settings"/"RiceMonkey"/"Modules";
    if(!fs::is_directory(directory)&&fs::is_directory(project/"ScriptModules"))return project/"ScriptModules";
    return directory;
}
std::string ScriptKey(const fs::path& project, const fs::path& source) {
    auto path=ScriptSourcePath(project,source);
    auto root=fs::absolute(project).lexically_normal();
    auto relative=path.lexically_relative(root);
    if(relative.empty()||*relative.begin()=="..")return Utf8(path);
    if(*relative.begin()=="Assets")return Utf8(path.lexically_relative(root/"Assets"));
    return *relative.begin()=="Scripts"?Utf8(relative):Utf8(path);
}
ModuleCatalog PublishedScripts(const fs::path& project) {
    ModuleCatalog result;
    auto library = ScriptModuleDirectory(project);
    if (!fs::is_directory(library)) return result;
    for (const auto& entry : fs::directory_iterator(library)) {
        if (!entry.is_directory() || !fs::is_regular_file(entry.path() / "current")) continue;
        auto [manifest,module] = PinPublication(entry.path());
        result.emplace(manifest.key, std::move(module));
    }
    return result;
}
bool SourceMatchesPublication(const fs::path& project, const fs::path& sourceFile, const std::string& source) {
    if (sourceFile.empty()) return false;
    auto key = ScriptKey(project,sourceFile);
    auto folder = ScriptModuleDirectory(project) / Bucket(key);
    if (!fs::is_regular_file(folder / "current")) return false;
    auto [info,module] = PinPublication(folder);
    auto snapshot = (folder / info.binary).replace_extension(".cpp");
    return info.key == key && fs::is_regular_file(snapshot) && Read(snapshot) == source;
}
void ExportScripts(const fs::path& project, const fs::path& destination) {
    auto library=ScriptModuleDirectory(project);
    auto output=ScriptModuleDirectory(destination);
    if (fs::is_regular_file(library/"build.id")) {
        fs::create_directories(output);
        fs::copy_file(library/"build.id",output/"build.id");
    }
    for (const auto& [key, module] : PublishedScripts(project)) {
        const auto& path=module.binary;
        auto folder = output / Bucket(key);
        fs::create_directories(folder);
        fs::copy_file(path, folder / path.filename());
        auto snapshot = fs::path(path).replace_extension(".cpp");
        if (fs::is_regular_file(snapshot)) fs::copy_file(snapshot, folder / snapshot.filename());
        std::ostringstream index;
        index << std::quoted(key) << ' ' << Utf8(path.filename()) << '\n';
        Write(folder / "current", index.str());
    }
}

void CompileAndSave(const fs::path& project, const fs::path& destination, const std::string& source) {
    const auto path = ScriptSourcePath(project, destination);
    const auto key = ScriptKey(project, path);
    const auto bucket = Bucket(key);
    const auto sdk = ModuleProject();
    // MSBuild's compiler probes create long tracker paths, independent of the
    // user's project and filename. Keep the intermediate build tree short.
    const auto cache=fs::temp_directory_path() / "RiceScripts";
    const auto work=cache / Bucket(Utf8(path)+"\n"+Utf8(sdk));
    const auto folder = ScriptModuleDirectory(project) / bucket;
    fs::create_directories(work);
    auto saveLock=cache / (Bucket(Utf8(path))+".lock");
    Handle lease{CreateFileW(saveLock.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (lease.h == INVALID_HANDLE_VALUE) throw std::runtime_error("Another editor is saving this script.");
    if (fs::exists(folder / "current") && ReadManifest(folder).key != key) throw std::runtime_error("Script cache key conflict.");
    const auto before = fs::exists(path) ? std::optional(Read(path)) : std::nullopt;
    Write(work / "candidate.cpp", NativeTranslationUnit(source,key));
    std::vector<std::wstring> configure{L"-S",sdk.wstring(),L"-B",work.wstring(),
        L"-DRICE_SOURCE_DIRECTORY=" + path.parent_path().wstring(), L"-DCMAKE_BUILD_TYPE=Release"};
    RunCMake(work, configure);
    fs::path driver=RICE_SCRIPT_CMAKE;
    RunCMake(work, {L"-E", L"env", L"VSLANG=1033", L"--", fs::is_regular_file(driver)?driver.wstring():L"cmake", L"--build", work.wstring(),
                    L"--config", L"Release", L"--target", L"script"});
    const auto after = fs::exists(path) ? std::optional(Read(path)) : std::nullopt;
    if (after != before) throw std::runtime_error("The source changed during compilation. Review the latest version and save again.");
    fs::create_directories(folder);
    auto buildId=folder.parent_path()/"build.id";
    if(!fs::exists(buildId)) {
        TemporaryFile pending{folder/(UniqueName()+".tmp")};
        Write(pending.path,UniqueName());
        if(!MoveFileW(pending.path.c_str(),buildId.c_str())&&!fs::is_regular_file(buildId))
            throw std::runtime_error("Cannot save the project's script build identity.");
    }
    fs::create_directories(path.parent_path());
    auto version = UniqueName();
    auto binary = version + ".dll";
    auto compiled = folder / binary;
    fs::copy_file(work / "out" / "script.dll", compiled);
    TemporaryFile manifest{folder / (version + ".tmp")};
    TemporaryFile staged{path.wstring() + L"." + fs::path(version).wstring() + L".tmp"};
    bool wroteSource = false;
    auto snapshot = fs::path(compiled).replace_extension(".cpp");
    try {
        Write(snapshot, source);
        std::ostringstream index;
        index << std::quoted(key) << ' ' << binary << '\n';
        Write(manifest.path, index.str());
        if (before != source) {
            Write(staged.path, source);
            Replace(staged.path, path);
            wroteSource = true;
        }
        Replace(manifest.path, folder / "current");
    } catch (...) {
        std::error_code ignored;
        fs::remove(compiled, ignored);
        fs::remove(snapshot, ignored);
        if (wroteSource && Read(path) == source) {
            if (before) { Write(staged.path,*before); Replace(staged.path,path); }
            else fs::remove(path,ignored);
        }
        throw;
    }
    RetireOlderBuilds(folder,compiled);
}
void SaveValidatedScript(const fs::path& project,const fs::path& destination,const std::string& source,
                         const std::function<void(const std::string&)>& warn) {
    ScriptDocument document;
    document.SetSource(source);
    const auto target=ScriptSourcePath(project,destination);
    std::map<std::string,std::vector<const ScriptNode*>> externalSignals;
    for(const auto& row:document.Nodes()){
        if(row.kind!=NodeKind::EmitEvent&&
           !((row.kind==NodeKind::Timer||row.kind==NodeKind::Trigger)&&(row.type=="Event"||row.type=="Function")))continue;
        const auto hash=row.value.rfind('#');
        constexpr std::string_view prefix="public:";
        if(row.value.starts_with(prefix)&&hash!=std::string::npos)
            externalSignals[row.value.substr(prefix.size(),hash-prefix.size())].push_back(&row);
    }
    std::vector<std::string> texts;
    for(const auto& file:ScriptFiles(project)){
        if(file.lexically_normal()==target)continue;
        texts.push_back(Read(file));
        const auto scriptKey=ScriptKey(project,file);
        auto pending=externalSignals.find(scriptKey);
        if(pending==externalSignals.end())continue;
        ScriptDocument provider;
        provider.SetSource(texts.back());
        for(const auto& symbol:provider.Nodes()){
            if(!symbol.isPublic||(symbol.kind!=NodeKind::Event&&symbol.kind!=NodeKind::Function))continue;
            const auto key=SignalKey(symbol,scriptKey);
            std::erase_if(pending->second,[&](const ScriptNode* use){
                const auto expected=use->kind==NodeKind::EmitEvent||use->type=="Event"?NodeKind::Event:NodeKind::Function;
                return symbol.kind==expected&&use->value==key;
            });
        }
    }
    std::vector<std::string_view> others(texts.begin(),texts.end());
    std::string errors;
    for(const auto& [file,uses]:externalSignals)for(const auto* use:uses)
        errors+=Utf8(target)+"("+std::to_string(use->line)+"): error: Selected public signal is missing or private in "+file+".\n";
    for (const auto& d:document.Validate(others)) {
        auto message=Utf8(target)+"("+std::to_string(d.line)+"): "+d.message;
        if(d.warning) warn(message);
        else errors+=message+"\n";
    }
    if (!errors.empty()) throw std::runtime_error(errors);
    CompileAndSave(project,destination,document.Source());
}
}
