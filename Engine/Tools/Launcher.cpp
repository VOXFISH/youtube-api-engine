#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "../Project/GameBundle.h"
#ifdef RICE_GAME_HOST
#include <objbase.h>
#else
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};

DWORD Run(const fs::path& program, const fs::path& workDir = {}, std::wstring command = GetCommandLineW()) {
#if defined(RICE_GAME_HOST) || defined(RICE_PLAYER_ENTRY)
    // A hosted player's lifetime follows its game EXE, including RiceCon force stop.
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Cannot prepare the Rice process");
#endif
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr,
                        workDir.empty() ? nullptr : workDir.c_str(), &startup, &info))
        throw std::runtime_error("Cannot start the Rice runtime. Check the installation files.");
    Handle process{info.hProcess}, thread{info.hThread};
#if defined(RICE_GAME_HOST) || defined(RICE_PLAYER_ENTRY)
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 1);
        throw std::runtime_error("Cannot attach the player to the game process");
    }
#endif
    if (ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(process.value, 1);
        throw std::runtime_error("Cannot start the Rice process");
    }
    DWORD code = 1;
    WaitForSingleObject(process.value, INFINITE);
    GetExitCodeProcess(process.value, &code);
    return code;
}
#ifdef RICE_GAME_HOST
struct GameDirectory {
    fs::path path;
    GameDirectory() {
        GUID id{};
        wchar_t unique[40]{};
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, unique, 40))
            throw std::runtime_error("Cannot allocate a game runtime directory");
        path = fs::temp_directory_path() / (std::wstring(L"RiceGame-") + unique);
        if (!fs::create_directory(path)) throw std::runtime_error("Game runtime directory already exists");
    }
    ~GameDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
#elif defined(RICE_EDITOR_ENTRY)
void RegisterProjectType(const fs::path& editor) {
    const wchar_t* type = L"Software\\Classes\\Rice.Project";
    const std::wstring command = L"\"" + editor.native() + L"\" \"%1\"";
    auto set = [](const std::wstring& key, const wchar_t* name, const std::wstring& value) {
        return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    set(type, nullptr, L"Rice Project");
    set(std::wstring(type) + L"\\shell\\open\\command", nullptr, command);
    set(L"Software\\Classes\\.rice\\OpenWithProgids", L"Rice.Project", L"");
    DWORD bytes{};
    if (RegGetValueW(HKEY_CLASSES_ROOT, L".rice", nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) {
        if (set(L"Software\\Classes\\.rice", nullptr, L"Rice.Project") == ERROR_SUCCESS)
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
}
#elif defined(RICE_PLAYER_ENTRY)
fs::path PickBuild(const fs::path& builds) {
    std::vector<wchar_t> name(32768);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = L"RicePlayer - Open game build";
    dialog.lpstrFilter = L"Rice game (*.exe)\0*.exe\0";
    dialog.lpstrInitialDir = builds.c_str();
    dialog.lpstrFile = name.data();
    dialog.nMaxFile = static_cast<DWORD>(name.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) return fs::path(name.data());
    if (CommDlgExtendedError()) throw std::runtime_error("Could not open the game selection window");
    return {};
}
std::wstring Quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
DWORD RunPlayer(const fs::path& installation) {
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) throw std::runtime_error("Cannot read player arguments");
    std::vector<std::wstring> args(argv + 1, argv + count);
    LocalFree(argv);
    fs::path game;
    size_t next = 0;
    if (args.empty()) game = PickBuild(installation / L"Builds");
    else if (args.front() == L"--build" && args.size() >= 2) { game = args[1]; next = 2; }
    else if (fs::path(args.front()).extension() == L".exe") { game = args.front(); next = 1; }
    else return Run(installation / "Engine" / "Runtime" / "RicePlayer.exe");
    if (game.empty()) return 0;
    game = fs::absolute(game);
    if (!Rice::IsGameBundle(game)) throw std::runtime_error("Select a standalone game exported by Rice");
    std::wstring command = Quote(game.native());
    for (; next < args.size(); ++next) command += L" " + Quote(args[next]);
    return Run(game, game.parent_path(), std::move(command));
}
#endif
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
    try {
        std::vector<wchar_t> path(32768);
        auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!n || n == path.size()) throw std::runtime_error("Cannot locate the Rice executable");
        fs::path self(std::wstring(path.data(), n));
#ifdef RICE_GAME_HOST
        GameDirectory extracted;
        Rice::ExtractGameBundle(self, extracted.path);
        return static_cast<int>(Run(extracted.path / "RicePlayer.exe", extracted.path));
#elif defined(RICE_EDITOR_ENTRY)
        if (!arguments || !*arguments) RegisterProjectType(self);
        return static_cast<int>(Run(self.parent_path() / "Engine" / "Runtime" / "Rice.exe"));
#elif defined(RICE_PLAYER_ENTRY)
        return static_cast<int>(RunPlayer(self.parent_path()));
#elif defined(RICE_CON_ENTRY)
        return static_cast<int>(Run(self.parent_path() / "Engine" / "Runtime" / "RiceCon.exe"));
#else
#error Select the Rice application entry point
#endif
    } catch (const std::exception& e) {
        if (arguments && wcsstr(arguments, L"--smoke")) {
            OutputDebugStringA(e.what());
        } else MessageBoxA(nullptr, e.what(), "Rice", MB_OK | MB_ICONERROR);
        return 1;
    }
}
