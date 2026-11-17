#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <Windows.h>
#include <sddl.h>
#include "Json.h"
#include <array>
#include <deque>
#include <optional>

namespace RiceController {
struct ControlMessage {
    int protocolVersion = 1;
    std::string runId, command, payload, state, windowMode, error;
    uint64_t requestId = 0, gameFrames = 0, renderFrames = 0;
    bool success = true;
    bool scriptReady = false;
    std::vector<std::string> capabilities;
};
inline std::string EncodeMessage(const ControlMessage& m) {
    Json::Object fields{{"protocolVersion",m.protocolVersion},{"runId",m.runId},{"requestId",m.requestId},
        {"command",m.command},{"payload",m.payload},{"state",m.state},{"windowMode",m.windowMode},
        {"error",m.error},{"success",m.success},{"gameFrames",m.gameFrames},{"renderFrames",m.renderFrames},{"scriptReady",m.scriptReady}};
    if(m.command=="Ready") fields.emplace("capabilities",Json::Array{"Pause","Resume","Stop","SetWindowMode","GetStatus","SaveForRetirement","RestoreSlot"});
    return Json(std::move(fields)).Dump();
}
inline ControlMessage DecodeMessage(const std::string& text) {
    if(text.size()>16384) throw std::runtime_error("Controller message exceeds 16 KiB");
    auto j=Json::Parse(text); ControlMessage m;
    auto version=j.At("protocolVersion").Integer();if(version!=1) throw std::runtime_error("Unsupported controller protocol");
    m.protocolVersion=1;m.runId=j.At("runId").String();m.command=j.At("command").String();
    auto request=j.At("requestId").Integer();if(request<0) throw std::runtime_error("Invalid controller request ID");m.requestId=uint64_t(request);
    auto stringField=[&](const char* key,std::string& output){if(auto* value=j.Find(key)) output=value->String();};
    stringField("payload",m.payload);stringField("state",m.state);stringField("windowMode",m.windowMode);stringField("error",m.error);
    if(auto* value=j.Find("success")) m.success=value->Boolean();
    if(auto* value=j.Find("scriptReady")) m.scriptReady=value->Boolean();
    if(auto* value=j.Find("capabilities")) for(const auto& item:value->Items())m.capabilities.push_back(item.String());
    if(auto* value=j.Find("gameFrames")){auto frames=value->Integer();if(frames<0)throw std::runtime_error("Invalid game frame count");m.gameFrames=uint64_t(frames);}
    if(auto* value=j.Find("renderFrames")){auto frames=value->Integer();if(frames<0)throw std::runtime_error("Invalid render frame count");m.renderFrames=uint64_t(frames);}
    if(m.runId.empty()||m.runId.size()>80||m.command.empty()||m.command.size()>64)throw std::runtime_error("Invalid controller message identity");
    return m;
}

// Message-mode PIPE_NOWAIT operations never block the application thread.
// A full pipe is retried from Receive/PollConnection, with a bounded outgoing queue.
class ControlPipe {
    HANDLE handle=INVALID_HANDLE_VALUE;
    bool server=false,connected=false;
    std::deque<std::string> outgoing;
    [[noreturn]] static void Error(const char* operation) { throw std::runtime_error(std::string(operation)+" (Win32 "+std::to_string(GetLastError())+")"); }
    void Flush() {
        while(connected&&!outgoing.empty()) {
            DWORD written=0;auto& bytes=outgoing.front();
            if(!WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)) {
                auto error=GetLastError();if(error==ERROR_PIPE_BUSY)return;
                Error("Controller pipe write failed");
            }
            if(!written)return;
            if(written!=bytes.size())throw std::runtime_error("Partial controller message write");
            outgoing.pop_front();
        }
    }
public:
    ControlPipe()=default;
    ~ControlPipe(){Close();}
    ControlPipe(const ControlPipe&)=delete;
    ControlPipe& operator=(const ControlPipe&)=delete;
    void Close() noexcept {
        if(handle!=INVALID_HANDLE_VALUE){if(server)DisconnectNamedPipe(handle);CloseHandle(handle);}
        handle=INVALID_HANDLE_VALUE;server=false;connected=false;outgoing.clear();
    }
    void OpenServer(const std::wstring& name) {
        Close();
        if(name.rfind(L"\\\\.\\pipe\\",0)!=0)throw std::runtime_error("A local controller pipe is required");
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))Error("Cannot inspect current user");
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> storage(size);
        if(!GetTokenInformation(token,TokenUser,storage.data(),size,&size)){CloseHandle(token);Error("Cannot inspect current user SID");}
        CloseHandle(token);
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&sid))Error("Cannot encode user SID");
        std::wstring descriptor=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR security=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(),SDDL_REVISION_1,&security,nullptr))Error("Cannot create pipe security");
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),security,FALSE};
        handle=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attributes);
        auto error=GetLastError();LocalFree(security);SetLastError(error);
        if(handle==INVALID_HANDLE_VALUE)Error("Cannot create controller pipe");server=true;
    }
    bool OpenClient(const std::wstring& name) {
        if(connected)return true;
        Close();
        if(name.rfind(L"\\\\.\\pipe\\",0)!=0)throw std::runtime_error("A local controller pipe is required");
        handle=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(handle==INVALID_HANDLE_VALUE){auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PIPE_BUSY)return false;Error("Cannot open controller pipe");}
        DWORD mode=PIPE_READMODE_MESSAGE|PIPE_NOWAIT;
        if(!SetNamedPipeHandleState(handle,&mode,nullptr,nullptr)){Close();Error("Cannot configure controller pipe");}
        connected=true;return true;
    }
    bool PollConnection() {
        if(!connected&&server) {
            // For PIPE_NOWAIT, TRUE only makes a disconnected pipe available.
            // ERROR_PIPE_CONNECTED, on a subsequent poll, confirms the client.
            // https://learn.microsoft.com/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe
            if(ConnectNamedPipe(handle,nullptr))return false;
            else {auto error=GetLastError();if(error==ERROR_PIPE_CONNECTED)connected=true;else if(error!=ERROR_PIPE_LISTENING)Error("Cannot connect controller pipe");}
        }
        Flush();return connected;
    }
    bool Connected()const{return connected;}
    DWORD ConnectedProcessId()const {
        ULONG pid=0;if(!connected)return 0;
        if(!(server?GetNamedPipeClientProcessId(handle,&pid):GetNamedPipeServerProcessId(handle,&pid)))Error("Cannot verify controller pipe peer");
        return pid;
    }
    void Send(const ControlMessage& message) {
        if(!connected)throw std::runtime_error("Controller pipe is not connected");
        auto text=EncodeMessage(message);if(text.size()>16384||outgoing.size()>=32)throw std::runtime_error("Controller pipe queue limit exceeded");
        outgoing.push_back(std::move(text));Flush();
    }
    std::optional<ControlMessage> Receive() {
        if(!PollConnection())return std::nullopt;
        DWORD available=0,remaining=0;
        if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,&remaining))Error("Controller pipe disconnected");
        if(!available)return std::nullopt;
        if(remaining>16384)throw std::runtime_error("Controller message exceeds 16 KiB");
        std::array<char,16384> bytes{};DWORD read=0;
        if(!ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))Error("Cannot read controller message");
        return DecodeMessage(std::string(bytes.data(),read));
    }
};
}
