#include "Rice/Benchmark.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Rice {
using Clock = std::chrono::steady_clock;
struct BenchmarkThreadRecorder {
    uint64_t generation=0,id=0,nextInvocation=0;
    bool gameThread=false;
    std::string name;
    std::vector<BenchmarkCall> calls;
    std::vector<BenchmarkScope*> scopes;
    Clock::time_point origin;
    int64_t ElapsedNs()const{return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-origin).count();}
    void CloseSegments(int64_t end){for(auto it=scopes.rbegin();it!=scopes.rend();++it)(*it)->Finish(end);}
    void OpenSegments(int64_t start){for(auto* scope:scopes)scope->BeginSegment(start,true);}
};
namespace {
thread_local std::shared_ptr<BenchmarkThreadRecorder> recording;
thread_local std::string requestedThreadName;
constexpr char BinaryMagic[]="RICEB001";
constexpr char TextHeader[]="frame\tstart_ns\tduration_ns\tself_ns\tdepth\tfile\tline\tfunction\tthread_id\tinvocation_id\tcontinuation\tthread_name\n";
std::string PathText(const std::filesystem::path& path){auto value=path.u8string();return {value.begin(),value.end()};}

template<class T>void AppendNumber(std::string& bytes,T value){
    using U=std::make_unsigned_t<T>;auto number=static_cast<U>(value);
    for(size_t i=0;i<sizeof(T);++i)bytes.push_back(static_cast<char>((number>>(i*8))&255));
}
template<class T>T ReadNumber(std::istream& source){
    std::array<unsigned char,sizeof(T)> bytes{};source.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    using U=std::make_unsigned_t<T>;U value=0;
    for(size_t i=0;i<bytes.size();++i)value|=static_cast<U>(bytes[i])<<(i*8);
    return static_cast<T>(value);
}
void AppendString(std::string& bytes,const char* value){
    auto size=std::char_traits<char>::length(value);
    if(size>std::numeric_limits<uint32_t>::max())throw std::runtime_error("Benchmark metadata is too large");
    AppendNumber(bytes,static_cast<uint32_t>(size));bytes.append(value,size);
}
std::string ReadString(std::istream& source){
    auto size=ReadNumber<uint32_t>(source);if(size>16*1024*1024)throw std::runtime_error("Invalid benchmark metadata length");
    std::string value(size,'\0');source.read(value.data(),size);return value;
}
void Archive(const std::filesystem::path& path){
    if(!std::filesystem::exists(path))return;
    if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Benchmark log path is not a regular file");
    auto stamp=std::to_string(Clock::now().time_since_epoch().count());
    auto archive=path;archive+=".archive-"+stamp;
    for(uint64_t attempt=1;std::filesystem::exists(archive);++attempt){archive=path;archive+=".archive-"+stamp+"-"+std::to_string(attempt);}
    std::filesystem::rename(path,archive);
}

// Explicit little-endian records; neither process pointers nor C++ struct padding
// enter the file. Source metadata is written once and calls refer to site IDs.
class BinaryLog {
    std::filesystem::path binaryPath;
    std::ofstream output;
    std::string block;
    using Site=std::tuple<const char*,const char*,uint32_t>;
    std::map<Site,uint32_t> sites;
    std::map<uint64_t,std::string> threads;
    uint64_t bytes=0;
    void Flush(){if(block.empty())return;output.write(block.data(),static_cast<std::streamsize>(block.size()));bytes+=block.size();block.clear();}
    uint32_t SiteID(const BenchmarkCall& call){
        Site key{call.file,call.function,call.line};auto found=sites.find(key);if(found!=sites.end())return found->second;
        auto id=static_cast<uint32_t>(sites.size());sites.emplace(key,id);
        AppendNumber<uint8_t>(block,1);AppendNumber(block,id);AppendNumber(block,call.line);AppendString(block,call.file);AppendString(block,call.function);
        if(block.size()>=64*1024)Flush();return id;
    }
public:
    explicit BinaryLog(const std::filesystem::path& text):binaryPath(text){
        binaryPath.replace_extension(".bin");if(binaryPath==text)binaryPath+=".capture";
        if(text.has_parent_path())std::filesystem::create_directories(text.parent_path());
        Archive(text);Archive(binaryPath);
        output.exceptions(std::ios::badbit|std::ios::failbit);output.open(binaryPath,std::ios::binary|std::ios::trunc);
        block.reserve(128*1024);block.append(BinaryMagic,8);AppendNumber<uint32_t>(block,1);AppendNumber<uint32_t>(block,0x01020304);
    }
    uint64_t Bytes()const{return bytes;}
    void Write(const BenchmarkFrameData& frame,const std::vector<BenchmarkThreadInfo>& names){
        for(const auto& thread:names){auto found=threads.find(thread.id);if(found!=threads.end()&&found->second==thread.name)continue;
            threads[thread.id]=thread.name;AppendNumber<uint8_t>(block,2);AppendNumber(block,thread.id);AppendString(block,thread.name.c_str());}
        for(const auto& call:frame.calls)SiteID(call);
        AppendNumber<uint8_t>(block,3);AppendNumber(block,frame.number);AppendNumber(block,frame.durationNs);AppendNumber<uint64_t>(block,frame.calls.size());
        for(const auto& call:frame.calls){
            AppendNumber(block,sites.at(Site{call.file,call.function,call.line}));AppendNumber(block,call.depth);
            AppendNumber(block,call.startNs);AppendNumber(block,call.durationNs);AppendNumber(block,call.selfNs);
            AppendNumber(block,call.threadId);AppendNumber(block,call.invocationId);AppendNumber<uint8_t>(block,call.continuation?1:0);
            if(block.size()>=64*1024)Flush();
        }
        Flush();output.flush();
    }
    void Finish(bool cancelled,uint32_t frames,uint64_t calls){
        AppendNumber<uint8_t>(block,4);AppendNumber<uint8_t>(block,cancelled?1:0);AppendNumber(block,frames);AppendNumber(block,calls);Flush();output.flush();output.close();
    }
};

// Conversion is incremental so a new diagnosis can pause it between bounded
// blocks. The worker prioritizes binary batches and never formats during capture.
class TextConversion {
    struct Site {uint32_t line;std::string file,function;};
    std::ifstream input;
    std::ofstream output;
    std::map<uint32_t,Site> sites;
    std::map<uint64_t,std::string> threads;
    uint64_t frame=0,remaining=0;
    std::string block;
    template<class T>void Number(T value){char digits[32];auto end=std::to_chars(digits,digits+sizeof(digits),value).ptr;block.append(digits,end);block+='\t';}
    void Flush(){if(!block.empty()){output.write(block.data(),static_cast<std::streamsize>(block.size()));block.clear();}}
public:
    TextConversion(const std::filesystem::path& binary,const std::filesystem::path& text){
        input.exceptions(std::ios::badbit|std::ios::failbit);input.open(binary,std::ios::binary);
        std::array<char,8> magic{};input.read(magic.data(),magic.size());
        if(std::string(magic.data(),8)!=BinaryMagic||ReadNumber<uint32_t>(input)!=1||ReadNumber<uint32_t>(input)!=0x01020304)throw std::runtime_error("Unsupported benchmark binary format");
        output.exceptions(std::ios::badbit|std::ios::failbit);output.open(text,std::ios::binary|std::ios::trunc);output<<TextHeader;block.reserve(128*1024);
    }
    bool Step(){
        for(size_t count=0;count<1024&&block.size()<128*1024;){
            if(remaining){
                auto site=ReadNumber<uint32_t>(input),depth=ReadNumber<uint32_t>(input);
                auto start=ReadNumber<int64_t>(input),duration=ReadNumber<int64_t>(input),self=ReadNumber<int64_t>(input);
                auto thread=ReadNumber<uint64_t>(input),invocation=ReadNumber<uint64_t>(input);auto continuation=ReadNumber<uint8_t>(input);
                const auto& metadata=sites.at(site);Number(frame);Number(start);Number(duration);Number(self);Number(depth);
                block+=metadata.file;block+='\t';Number(metadata.line);block+=metadata.function;block+='\t';Number(thread);Number(invocation);
                block+=continuation?'1':'0';block+='\t';block+=threads.at(thread);block+='\n';--remaining;++count;
                continue;
            }
            auto kind=ReadNumber<uint8_t>(input);
            if(kind==1){auto id=ReadNumber<uint32_t>(input);Site site;site.line=ReadNumber<uint32_t>(input);site.file=ReadString(input);site.function=ReadString(input);sites[id]=std::move(site);}
            else if(kind==2){auto id=ReadNumber<uint64_t>(input);threads[id]=ReadString(input);}
            else if(kind==3){frame=ReadNumber<uint64_t>(input);(void)ReadNumber<int64_t>(input);remaining=ReadNumber<uint64_t>(input);}
            else if(kind==4){(void)ReadNumber<uint8_t>(input);(void)ReadNumber<uint32_t>(input);(void)ReadNumber<uint64_t>(input);Flush();output.flush();return true;}
            else throw std::runtime_error("Invalid benchmark binary record");
        }
        Flush();return false;
    }
};
struct Capture {
    std::shared_ptr<BenchmarkResult> result=std::make_shared<BenchmarkResult>();
    std::filesystem::path text,binary;
    std::atomic<bool> cancelled{false};
    bool finishQueued=false,writeFailed=false;
    uint32_t writtenFrames=0;
    uint64_t writtenCalls=0,binaryBytes=0;
    std::unique_ptr<BinaryLog> log;
    std::unique_ptr<TextConversion> conversion;
};
struct Batch {
    BenchmarkFrameData frame;
    std::vector<std::vector<BenchmarkCall>> chunks;
    std::vector<BenchmarkThreadInfo> threads;
    std::shared_ptr<Capture> capture;
    bool finish=false,hasFrame=false;
};
struct Runtime {
    std::shared_mutex frameGate;
    std::atomic<uint64_t> generation{0};
    uint64_t nextGeneration=0,captureNumber=0;
    std::atomic<bool> playing{false},enabled{false},collecting{false};
    std::atomic<uint64_t> gameFrame{0};
    bool frameOpen=false;
    uint64_t frameNumber=0;
    Clock::time_point origin;
    std::vector<std::shared_ptr<BenchmarkThreadRecorder>> recorders;
    std::mutex mutex,exportGate;
    std::condition_variable ready;
    std::deque<Batch> queue;
    std::deque<std::shared_ptr<Capture>> exports;
    std::vector<std::vector<BenchmarkCall>> spareBuffers;
    bool stopping=false;
    std::thread worker;
    std::filesystem::path logPath;
    uint64_t nextSample=0;
    uint32_t interval=0;
    BenchmarkStatus status;
    std::shared_ptr<Capture> pending;
    std::shared_ptr<const BenchmarkResult> result;
    ~Runtime(){if(worker.joinable())Benchmark::Shutdown();}
    bool IsGameThread()const{return recording&&recording->gameThread&&recording->generation==generation.load();}
    void RegisterThread(bool main=false){
        recording=std::make_shared<BenchmarkThreadRecorder>();recording->generation=generation.load();recording->id=recorders.size();recording->gameThread=main;
        recording->name=requestedThreadName.empty()?(main?"Game":"Worker "+std::to_string(recording->id)):requestedThreadName;
        recording->origin=origin;recorders.push_back(recording);
    }
    void StartSample(){
        auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-origin).count();
        for(auto& recorder:recorders){recorder->origin=origin;recorder->OpenSegments(elapsed);}
        enabled.store(true,std::memory_order_release);
    }
    void ForgetScopes(){for(auto& recorder:recorders)recorder->scopes.clear();}
    Batch Collect(Clock::time_point end){
        Batch batch;batch.capture=pending;batch.hasFrame=true;batch.frame.number=frameNumber;
        batch.frame.durationNs=std::chrono::duration_cast<std::chrono::nanoseconds>(end-origin).count();
        for(auto& recorder:recorders){
            recorder->CloseSegments(batch.frame.durationNs);batch.threads.push_back({recorder->id,recorder->name});
            if(!recorder->calls.empty()){
                batch.chunks.push_back(std::move(recorder->calls));recorder->calls={};
                if(!spareBuffers.empty()){recorder->calls=std::move(spareBuffers.back());spareBuffers.pop_back();}
            }
        }
        return batch;
    }
    void Enqueue(Batch batch,std::unique_lock<std::mutex>& lock){
        ready.wait(lock,[this]{return queue.size()<8;});queue.push_back(std::move(batch));ready.notify_all();
    }
    void Error(const std::shared_ptr<Capture>& capture,const std::exception& error){
        std::lock_guard lock(mutex);if(pending==capture)status.logError=PathText(capture->text)+" - "+error.what();
    }
    void FinishStatus(const std::shared_ptr<Capture>& capture){
        std::lock_guard lock(mutex);
        if(pending==capture){status.active=false;status.finalizing=false;pending.reset();}
        ready.notify_all();
    }
    void WriteFrames(){
        for(;;){
            std::unique_lock lock(mutex);
            ready.wait(lock,[this]{return stopping||!queue.empty()||(!collecting.load()&&!exports.empty());});
            if(queue.empty()){
                if(exports.empty()){if(stopping)break;continue;}
                if(collecting.load())continue;
                auto capture=exports.front();lock.unlock();bool done=false,paused=false;
                try{
                    std::lock_guard conversionLock(exportGate);
                    if(collecting.load())paused=true;
                    else{
                        if(!capture->conversion)capture->conversion=std::make_unique<TextConversion>(capture->binary,capture->text);
                        done=capture->conversion->Step();
                    }
                }catch(const std::exception& error){Error(capture,error);done=true;}
                if(paused)continue;
                if(done){capture->conversion.reset();{std::lock_guard finish(mutex);exports.pop_front();}FinishStatus(capture);}
                continue;
            }
            Batch batch=std::move(queue.front());queue.pop_front();lock.unlock();ready.notify_all();auto capture=batch.capture;
            if(batch.hasFrame){
                if(batch.chunks.size()==1)batch.frame.calls=std::move(batch.chunks.front());
                else if(!batch.chunks.empty()){
                    size_t count=0;for(auto& chunk:batch.chunks)count+=chunk.size();batch.frame.calls.reserve(count);
                    for(auto& chunk:batch.chunks){batch.frame.calls.insert(batch.frame.calls.end(),chunk.begin(),chunk.end());chunk.clear();}
                    std::stable_sort(batch.frame.calls.begin(),batch.frame.calls.end(),[](const auto& a,const auto& b){return a.startNs<b.startNs;});
                }
                if(!capture->writeFailed)try{
                    if(!capture->log)capture->log=std::make_unique<BinaryLog>(capture->text);
                    capture->log->Write(batch.frame,batch.threads);capture->binaryBytes=capture->log->Bytes();
                }catch(const std::exception& error){capture->writeFailed=true;capture->log.reset();Error(capture,error);}
                ++capture->writtenFrames;capture->writtenCalls+=batch.frame.calls.size();
                capture->result->frames.push_back(std::move(batch.frame));capture->result->threads=std::move(batch.threads);
                std::lock_guard progress(mutex);
                if(pending==capture){status.writtenCalls=capture->writtenCalls;status.binaryBytes=capture->binaryBytes;}
            }
            if(batch.finish){
                bool exportReady=false;
                if(capture->log)try{
                    capture->log->Finish(capture->cancelled.load(),capture->writtenFrames,capture->writtenCalls);
                    capture->binaryBytes=capture->log->Bytes();capture->log.reset();exportReady=true;
                }catch(const std::exception& error){capture->log.reset();Error(capture,error);}
                {
                    std::lock_guard publish(mutex);
                    if(pending==capture){
                        status.binaryBytes=capture->binaryBytes;
                        if(!capture->cancelled.load())result=capture->result;
                    }
                    if(exportReady)exports.push_back(capture);
                }
                // Binary conversion does not need the retained graph data. A
                // cancelled capture in particular should release it promptly.
                capture->result.reset();
                if(!exportReady)FinishStatus(capture);
            }
            // Small scratch buffers are useful; large diagnosis allocations must
            // not remain resident for the rest of the editor session.
            std::lock_guard recycle(mutex);
            for(auto& chunk:batch.chunks)if(chunk.capacity()&&chunk.capacity()*sizeof(BenchmarkCall)<=4*1024*1024&&spareBuffers.size()<8){chunk.clear();spareBuffers.push_back(std::move(chunk));}
        }
    }
};
Runtime& State(){static Runtime runtime;return runtime;}
}

void Benchmark::Initialize(std::filesystem::path logFile){
    auto& state=State();std::unique_lock gate(state.frameGate);std::lock_guard lock(state.mutex);
    if(state.worker.joinable())throw std::logic_error("Benchmark session already running");
    state.stopping=false;state.status={};state.result.reset();state.pending.reset();state.recorders.clear();state.spareBuffers.clear();state.queue.clear();state.exports.clear();
    state.frameNumber=0;state.frameOpen=false;state.gameFrame=0;state.playing=false;state.enabled=false;state.collecting=false;state.logPath=std::move(logFile);state.captureNumber=0;
    state.origin=Clock::now();state.generation=++state.nextGeneration;state.RegisterThread(true);state.worker=std::thread([&state]{state.WriteFrames();});
}
void Benchmark::Shutdown(){
    auto& state=State();if(!state.IsGameThread())return;EndFrame();
    std::unique_lock gate(state.frameGate);std::unique_lock lock(state.mutex);
    state.enabled=false;state.collecting=false;
    if(state.pending&&!state.pending->finishQueued){
        state.pending->cancelled=true;state.pending->finishQueued=true;Batch end;end.capture=state.pending;end.finish=true;state.Enqueue(std::move(end),lock);
    }
    state.ForgetScopes();state.generation=0;state.playing=false;state.gameFrame=0;state.recorders.clear();recording.reset();state.stopping=true;
    lock.unlock();gate.unlock();state.ready.notify_all();state.worker.join();
    std::lock_guard finish(state.mutex);state.pending.reset();state.status.active=false;state.status.finalizing=false;state.spareBuffers.clear();
}
void Benchmark::BeginPlay(){auto& state=State();if(!state.IsGameThread()||state.playing.exchange(true))return;state.gameFrame=0;}
void Benchmark::EndPlay(){
    auto& state=State();if(!state.IsGameThread())return;EndFrame();
    bool incomplete;{std::lock_guard lock(state.mutex);incomplete=state.pending&&!state.pending->finishQueued;}
    if(incomplete)Cancel();state.playing=false;state.gameFrame=0;
}
bool Benchmark::IsPlaying(){return State().playing.load();}
void Benchmark::SetThreadName(std::string name){
    std::replace(name.begin(),name.end(),'\t',' ');std::replace(name.begin(),name.end(),'\n',' ');std::replace(name.begin(),name.end(),'\r',' ');requestedThreadName=std::move(name);
    auto& state=State();std::unique_lock gate(state.frameGate);if(!state.generation.load())return;
    if(!recording||recording->generation!=state.generation.load())state.RegisterThread();if(!requestedThreadName.empty())recording->name=requestedThreadName;
}
void Benchmark::BeginFrame(){
    auto& state=State();if(!state.IsGameThread())return;if(state.frameOpen)EndFrame();
    std::unique_lock gate(state.frameGate);std::lock_guard lock(state.mutex);
    state.frameNumber=state.playing?++state.gameFrame:0;state.frameOpen=true;state.origin=Clock::now();
    if(state.collecting.load()&&state.frameNumber==state.nextSample)state.StartSample();
}
void Benchmark::EndFrame(){
    auto& state=State();if(!state.IsGameThread()||!state.frameOpen)return;
    std::unique_lock gate(state.frameGate);std::unique_lock lock(state.mutex);
    if(state.enabled.exchange(false)){
        auto batch=state.Collect(Clock::now());
        if(++state.status.sampled==state.status.requested){
            batch.finish=true;state.pending->finishQueued=true;state.collecting=false;state.status.finalizing=true;state.ForgetScopes();
        }else state.nextSample+=state.interval;
        state.Enqueue(std::move(batch),lock);
    }
    state.frameOpen=false;state.frameNumber=0;
}
uint64_t Benchmark::CurrentFrame(){return State().gameFrame.load();}
bool Benchmark::Schedule(uint64_t start,uint32_t frames,uint32_t intervals){
    auto& state=State();if(!state.IsGameThread()||!start||!frames||!intervals)return false;
    auto current=state.gameFrame.load();if(start<current||(start==current&&!state.frameNumber))return false;
    uint64_t span=uint64_t(frames-1)*intervals;if(start>std::numeric_limits<uint64_t>::max()-span)return false;
    std::unique_lock gate(state.frameGate);std::lock_guard lock(state.mutex);if(state.status.active)return false;
    // A converter step holds this only for a bounded block. No text conversion
    // overlaps the newly activated diagnostic, including its interval gaps.
    std::lock_guard exportLock(state.exportGate);state.collecting=true;
    state.ForgetScopes();state.pending=std::make_shared<Capture>();state.pending->text=state.logPath;
    // An earlier cancelled capture may still be converting. Its file names stay
    // immutable so rescheduling cannot overwrite a queued reader's input.
    if(++state.captureNumber>1){auto stem=state.logPath.stem();stem+=".capture-"+std::to_string(state.captureNumber);stem+=state.logPath.extension();state.pending->text=state.logPath.parent_path()/stem;}
    state.pending->binary=state.pending->text;state.pending->binary.replace_extension(".bin");
    if(state.pending->binary==state.pending->text)state.pending->binary+=".capture";
    state.nextSample=start;state.interval=intervals;state.status={};state.status.active=true;state.status.requested=frames;
    state.status.textFile=PathText(state.pending->text);state.status.binaryFile=PathText(state.pending->binary);
    if(state.frameOpen&&state.frameNumber==start)state.StartSample();return true;
}
void Benchmark::Cancel(){
    auto& state=State();if(!state.IsGameThread())return;std::unique_lock gate(state.frameGate);std::unique_lock lock(state.mutex);
    if(!state.pending)return;auto capture=state.pending;capture->cancelled=true;
    if(!capture->finishQueued){
        Batch batch;if(state.enabled.exchange(false))batch=state.Collect(Clock::now());else batch.capture=capture;
        batch.finish=true;capture->finishQueued=true;state.Enqueue(std::move(batch),lock);
    }
    state.enabled=false;state.collecting=false;state.ForgetScopes();state.status.active=false;state.status.finalizing=true;state.ready.notify_all();
}
BenchmarkStatus Benchmark::GetStatus(){auto& state=State();std::lock_guard lock(state.mutex);auto status=state.status;status.playing=state.playing.load();status.recording=state.enabled.load();return status;}
std::shared_ptr<const BenchmarkResult> Benchmark::Result(){auto& state=State();std::lock_guard lock(state.mutex);return state.result;}

BenchmarkScope::BenchmarkScope(const char* label,const char* source,uint32_t sourceLine):function(label),file(source),line(sourceLine){
    auto& state=State();if(!state.enabled.load(std::memory_order_acquire))return;
    auto generation=state.generation.load(std::memory_order_acquire);if(!generation)return;
    std::shared_lock<std::shared_mutex> gate;
    if(!recording||!recording->gameThread||recording->generation!=generation){
        gate=std::shared_lock(state.frameGate);generation=state.generation.load();if(!generation||!state.enabled.load())return;
        if(!recording||recording->generation!=generation){
            gate.unlock();{std::unique_lock registration(state.frameGate);if(!state.generation.load()||!state.enabled.load())return;state.RegisterThread();}
            gate.lock();if(recording->generation!=state.generation.load()||!state.enabled.load())return;
        }
    }
    recorder=recording;invocationId=++recorder->nextInvocation;parent=recorder->scopes.empty()?nullptr:recorder->scopes.back();
    BeginSegment(recorder->ElapsedNs(),false);recorder->scopes.push_back(this);
}
void BenchmarkScope::BeginSegment(int64_t startNs,bool continuation){
    index=recorder->calls.size();auto depth=parent&&parent->active?recorder->calls[parent->index].depth+1:0;
    recorder->calls.push_back({function,file,line,depth,startNs,0,0,recorder->id,invocationId,continuation});active=true;
}
void BenchmarkScope::Finish(int64_t endNs){
    if(!active)return;auto& call=recorder->calls[index];call.durationNs=endNs-call.startNs;call.selfNs+=call.durationNs;
    if(parent&&parent->active)recorder->calls[parent->index].selfNs-=call.durationNs;active=false;
}
BenchmarkScope::~BenchmarkScope(){
    if(!recorder)return;std::shared_lock<std::shared_mutex> gate;if(!recorder->gameThread)gate=std::shared_lock(State().frameGate);
    if(active)Finish(recorder->ElapsedNs());
    if(!recorder->scopes.empty()&&recorder->scopes.back()==this)recorder->scopes.pop_back();
    else{auto found=std::find(recorder->scopes.begin(),recorder->scopes.end(),this);if(found!=recorder->scopes.end())recorder->scopes.erase(found);}
}
}
