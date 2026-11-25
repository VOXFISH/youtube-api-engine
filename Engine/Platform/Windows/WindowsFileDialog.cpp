#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include "Platform/FileDialog.h"
#include "Rice/Benchmark.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Rice {
std::optional<std::filesystem::path> PickProjectFile(const std::filesystem::path& directory){
    RICE_BENCHMARK_FUNCTION();
    std::vector<wchar_t> buffer(32768,L'\0');auto initial=directory.native();
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFilter=L"Rice projects (*.rice)\0*.rice\0\0";
    dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir=initial.c_str();dialog.lpstrTitle=L"Open Rice Project";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(GetOpenFileNameW(&dialog))return std::filesystem::path(buffer.data());
    if(CommDlgExtendedError())throw std::runtime_error("Cannot open project dialog");
    return std::nullopt;
}
std::optional<std::filesystem::path> PickScriptEditor(){
    std::vector<wchar_t> buffer(32768,L'\0');
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFilter=L"Applications (*.exe)\0*.exe\0\0";
    dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle=L"Choose script editor";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(GetOpenFileNameW(&dialog))return std::filesystem::path(buffer.data());
    if(CommDlgExtendedError())throw std::runtime_error("Cannot open script editor dialog");
    return std::nullopt;
}
std::optional<std::filesystem::path> PickScriptFile(const std::filesystem::path& directory){
    std::vector<wchar_t> buffer(32768,L'\0');auto initial=directory.native();
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFilter=L"C++ scripts (*.h;*.hpp;*.cpp;*.cc;*.cxx)\0*.h;*.hpp;*.cpp;*.cc;*.cxx\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir=initial.c_str();dialog.lpstrTitle=L"Load C++ script";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(GetOpenFileNameW(&dialog))return std::filesystem::path(buffer.data());
    if(CommDlgExtendedError())throw std::runtime_error("Cannot open script file dialog");
    return std::nullopt;
}
void OpenScriptFile(const std::filesystem::path& script,const std::filesystem::path& editor){
    auto source=std::filesystem::absolute(script);
    if(!std::filesystem::is_regular_file(source))throw std::runtime_error("Script source file is unavailable: "+source.string());
    auto arguments=L"\""+source.native()+L"\"";
    auto target=editor.empty()?source:editor;
    auto result=ShellExecuteW(GetActiveWindow(),L"open",target.c_str(),editor.empty()?nullptr:arguments.c_str(),nullptr,SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(result)<=32)throw std::runtime_error("Cannot open script. Choose an editor in Options > Script editor.");
}
static std::optional<std::filesystem::path> PickDocument(bool save,const std::filesystem::path& suggested,
        const wchar_t* filter,const wchar_t* extension,const wchar_t* title){
    RICE_BENCHMARK_FUNCTION();
    auto absolute=suggested.empty()?std::filesystem::path{}:std::filesystem::absolute(suggested);
    auto directory=absolute.parent_path();
    while(!directory.empty()&&!std::filesystem::is_directory(directory)){
        auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
    }
    auto initialDirectory=directory.native();auto filename=save?absolute.filename().native():std::wstring{};
    std::vector<wchar_t> buffer(32768,L'\0');
    if(filename.size()>=buffer.size())throw std::runtime_error("The filename is too long.");
    std::copy(filename.begin(),filename.end(),buffer.begin());
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
    dialog.lpstrFilter=filter;
    dialog.nFilterIndex=1;dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir=initialDirectory.empty()?nullptr:initialDirectory.c_str();dialog.lpstrDefExt=extension;
    dialog.lpstrTitle=title;
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))return std::filesystem::path(buffer.data());
    auto error=CommDlgExtendedError();if(error)throw std::runtime_error("Cannot open the file dialog ("+std::to_string(error)+")");
    return std::nullopt;
}
std::optional<std::filesystem::path> PickAssetFile(){
    return PickDocument(false,{},L"FBX models (*.fbx)\0*.fbx\0All assets (*.*)\0*.*\0\0",nullptr,L"Import asset");
}
std::optional<std::filesystem::path> PickShapeFile(bool save,const std::filesystem::path& suggested){
    return PickDocument(save,suggested,L"Rice Shape (*.riceshape)\0*.riceshape\0All files (*.*)\0*.*\0\0",
                        L"riceshape",save?L"Save Shape":L"Load Shape");
}
std::optional<std::filesystem::path> PickScriptDestination(const std::filesystem::path& suggested,bool preset){
    return PickDocument(true,suggested,L"C++ source (*.cpp)\0*.cpp\0All files (*.*)\0*.*\0\0",
                        L"cpp",preset?L"Save as Preset":L"Save Script");
}
}
