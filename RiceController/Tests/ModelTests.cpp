#include "ControllerModel.h"
#include "../Shared/ControlProtocol.h"
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace RiceController;
namespace fs=std::filesystem;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>static void Reject(F fn,const char* message){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}Check(rejected,message);}
static std::string Read(const fs::path& path){std::ifstream stream(path,std::ios::binary);std::ostringstream result;result<<stream.rdbuf();return result.str();}
int main(int argc,char** argv){try{
    auto root=(argc>1?fs::path(argv[1]):fs::path("TestData"))/NewId();fs::create_directories(root/"Project"/"Assets");
    {std::ofstream(root/"RicePlayer.exe")<<"test fixture";std::ofstream(root/"Project"/"project.rice")<<"RICE_PROJECT 1 0\n";}
    std::vector<Entry> entries{{NewId(),"빌드 1",root,root/"Project"},{NewId(),"빌드 3",root,root/"Project"}};
    Check(DefaultName(entries)=="빌드 2","Smallest unused build number");
    entries[0].name="사용자 이름";Check(DefaultName(entries)=="빌드 1","Renamed default name can be reused");
    entries[0].name="빌드 01";Check(DefaultName(entries)=="빌드 1","Leading zeros do not reserve the canonical name");
    std::string name="  한글 이름  ";ValidateName(name);Check(name=="한글 이름","Name trimming preserves Korean");
    Reject([]{std::string value=" \t ";ValidateName(value);},"Empty name rejected");
    Reject([]{std::string value="a\nb";ValidateName(value);},"Control characters rejected");
    Reject([]{std::string value(65,'a');ValidateName(value);},"Overlong name rejected");
    Reject([]{std::string value="\xff";ValidateName(value);},"Invalid UTF8 rejected");
    std::string unicode;for(int i=0;i<64;++i)unicode+="한";ValidateName(unicode);
    auto entry=entries[0];entry.name="검증 빌드";entry.project/=L"project.rice";ValidateEntry(entry);
    Check(entry.executable==fs::absolute(root/"RicePlayer.exe"),"Build folder resolves player executable");
    Check(entry.project==fs::absolute(root/"Project"),"Project file resolves folder");
    auto missing=entry;missing.executable=root/"missing.exe";Reject([&]{ValidateEntry(missing);},"Missing executable rejected");
    auto config=root/L"설정 폴더"/L"controller.json";SaveEntries(config,{entry});
    auto loaded=LoadEntries(config);Check(loaded.size()==1&&loaded[0].id==entry.id&&loaded[0].name==entry.name&&loaded[0].project==entry.project,"Settings roundtrip keeps Unicode and identity");
    entry.windowMode="hidden";SaveEntries(config,{entry});Check(LoadEntries(config)[0].windowMode=="hidden","Atomic settings replacement");
    for(auto& file:fs::directory_iterator(config.parent_path()))Check(file.path().extension()!=L".tmp","No temporary settings remain");
    missing=entry;missing.executable=root/"moved.exe";SaveEntries(config,{missing});Check(LoadEntries(config).size()==1,"Moved build remains registered");
    std::string corrupted="{\"schemaVersion\":1,\"entries\":[";{std::ofstream stream(config,std::ios::binary|std::ios::trunc);stream<<corrupted;}
    Reject([&]{LoadEntries(config);},"Corrupt settings rejected");Check(Read(config)==corrupted,"Corrupt original preserved");
    Check(LoadEntries(root/"absent.json").empty(),"First launch has no entries");
    Reject([]{Json::Parse("{\"x\":1,\"x\":2}");},"Duplicate JSON fields rejected");
    Reject([]{Json::Parse("[1,]");},"Trailing array comma rejected");
    Reject([]{Json::Parse("\"\\ud800\"");},"Unpaired surrogate rejected");
    Check(Json::Parse("\"\\ud55c\\uae00\"").String()=="한글","JSON Unicode escapes decoded");
    Check(Json::Parse("\"\\ud83d\\ude00\"").String()=="\xf0\x9f\x98\x80","JSON surrogate pairs decoded");
    ControlMessage message;message.runId=NewId();message.command="SetWindowMode";message.payload="hidden";message.requestId=7;message.gameFrames=42;message.renderFrames=80;
    auto decoded=DecodeMessage(EncodeMessage(message));Check(decoded.runId==message.runId&&decoded.payload=="hidden"&&decoded.requestId==7&&decoded.gameFrames==42&&decoded.renderFrames==80,"Protocol roundtrip");
    message.protocolVersion=9;Reject([&]{DecodeMessage(EncodeMessage(message));},"Unknown protocol version rejected");
    ControlPipe server,client;auto pipeName=L"\\\\.\\pipe\\RiceController-test-"+Wide(NewId());
    server.OpenServer(pipeName);Check(!server.PollConnection(),"A listening pipe is not a connected client");
    Check(client.OpenClient(pipeName),"Local pipe client opens");Check(server.PollConnection(),"Server confirms actual client connection");
    Check(server.ConnectedProcessId()==GetCurrentProcessId(),"Connected peer PID verified");
    message.protocolVersion=1;client.Send(message);auto received=server.Receive();
    Check(received&&received->requestId==7&&received->payload=="hidden","Nonblocking pipe transports complete message");
    server.Send(*received);auto reply=client.Receive();Check(reply&&reply->requestId==7,"Nonblocking pipe reply");
    for(auto& argument:std::vector<std::wstring>{L"",L"plain",L"C:\\한글 폴더\\",L"one\"two",L"back\\\\\"quote"}){
        auto command=L"app.exe "+QuoteArgument(argument);int count=0;auto parsed=CommandLineToArgvW(command.c_str(),&count);
        Check(parsed&&count==2,"Quoted command parsed");std::wstring actual=parsed[1];LocalFree(parsed);Check(actual==argument,"Windows argument quoting roundtrip");
    }
    std::cout<<"Controller model and protocol tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
