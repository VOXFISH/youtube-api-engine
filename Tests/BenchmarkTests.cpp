#include "Rice/Benchmark.h"
#include "Editor/BenchmarkBands.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <semaphore>
#include <sstream>
#include <map>

using namespace Rice;
using namespace std::chrono_literals;

namespace {
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template<class F> void WaitUntil(F condition, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!condition() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    Check(condition(), message);
}

void WaitForFinalization()
{
    WaitUntil([] { const auto status=Benchmark::GetStatus();return !status.active&&!status.finalizing; }, "Diagnostic finalization did not complete");
}

template<class T> T ReadLittle(std::istream& input)
{
    uint64_t value=0;
    for(size_t i=0;i<sizeof(T);++i){const int byte=input.get();Check(byte!=EOF,"Truncated binary record");value|=uint64_t(uint8_t(byte))<<(i*8);}
    return static_cast<T>(value);
}

std::string ReadBinaryString(std::istream& input)
{
    auto length=ReadLittle<uint32_t>(input);Check(length<1024*1024,"Unbounded binary metadata length");
    std::string value(length,'\0');input.read(value.data(),length);Check(bool(input),"Truncated binary metadata");return value;
}

// Independently decode the documented wire format. Comparing every TSV column
// catches accidentally writing process pointers, native struct padding or IDs
// without their dictionary entries, and verifies the delayed converter.
void CheckBinaryAndText(const std::filesystem::path& binary,const std::filesystem::path& text,bool expectedCancelled=false)
{
    std::ifstream input(binary,std::ios::binary);Check(bool(input),"Binary diagnosis file missing");
    char magic[8];input.read(magic,8);Check(input.gcount()==8&&std::string(magic,8)=="RICEB001","Incorrect binary magic");
    Check(ReadLittle<uint32_t>(input)==1&&ReadLittle<uint32_t>(input)==0x01020304,"Incorrect binary version or byte order");
    struct Site {uint32_t line;std::string file,function;};
    std::map<uint32_t,Site> sites;std::map<uint64_t,std::string> threads;
    std::vector<std::vector<std::string>> expected;
    uint32_t frames=0;uint64_t calls=0;bool ended=false;
    while(!ended){
        auto type=ReadLittle<uint8_t>(input);
        if(type==1){
            auto id=ReadLittle<uint32_t>(input);auto line=ReadLittle<uint32_t>(input);auto file=ReadBinaryString(input);auto function=ReadBinaryString(input);
            Check(!sites.contains(id),"Duplicate binary site identifier");sites.emplace(id,Site{line,std::move(file),std::move(function)});
        }else if(type==2){
            auto id=ReadLittle<uint64_t>(input);threads[id]=ReadBinaryString(input);
        }else if(type==3){
            auto frame=ReadLittle<uint64_t>(input);auto frameDuration=ReadLittle<int64_t>(input);auto count=ReadLittle<uint64_t>(input);++frames;
            for(uint64_t i=0;i<count;++i){
                auto site=ReadLittle<uint32_t>(input);auto depth=ReadLittle<uint32_t>(input);
                auto start=ReadLittle<int64_t>(input),duration=ReadLittle<int64_t>(input),self=ReadLittle<int64_t>(input);
                auto thread=ReadLittle<uint64_t>(input),invocation=ReadLittle<uint64_t>(input);auto continuation=ReadLittle<uint8_t>(input);
                Check(sites.contains(site)&&threads.contains(thread),"Binary call references missing metadata");
                Check(start>=0&&duration>=0&&self>=0&&self<=duration&&start+duration<=frameDuration,"Invalid binary call timing");
                const auto& location=sites.at(site);
                expected.push_back({std::to_string(frame),std::to_string(start),std::to_string(duration),std::to_string(self),std::to_string(depth),
                    location.file,std::to_string(location.line),location.function,std::to_string(thread),std::to_string(invocation),std::to_string(continuation),threads.at(thread)});
                ++calls;
            }
        }else if(type==4){
            auto cancelled=ReadLittle<uint8_t>(input);Check(cancelled==uint8_t(expectedCancelled),"Incorrect binary completion state");
            Check(ReadLittle<uint32_t>(input)==frames&&ReadLittle<uint64_t>(input)==calls,"Binary completion counters do not match its records");ended=true;
        }else Check(false,"Unknown binary record type");
    }
    Check(input.peek()==EOF,"Unexpected data after binary completion marker");
    std::ifstream output(text);Check(bool(output),"Converted text diagnosis file missing");
    std::string line;std::getline(output,line);
    Check(line=="frame\tstart_ns\tduration_ns\tself_ns\tdepth\tfile\tline\tfunction\tthread_id\tinvocation_id\tcontinuation\tthread_name","Incorrect converted TSV header");
    size_t row=0;
    while(std::getline(output,line)){
        std::istringstream fields(line);std::vector<std::string> actual;std::string field;
        while(std::getline(fields,field,'\t'))actual.push_back(field);
        Check(row<expected.size()&&actual==expected[row],"Converted TSV lost binary call metadata, timing or order");++row;
    }
    Check(row==expected.size(),"Converted TSV omitted binary calls");
}

void CheckDiagnosticOnly(const std::filesystem::path& directory)
{
    const auto text=directory/"diagnostic-only.tsv";auto binary=text;binary.replace_extension(".bin");
    BenchmarkSession session(text);
    for(int i=0;i<5;++i){BenchmarkFrame frame;BenchmarkScope idle("Editor idle","Game/Only.cpp",1);}
    Benchmark::BeginPlay();
    {BenchmarkFrame frame;BenchmarkScope unarmed("Unarmed game","Game/Only.cpp",2);}
    Check(!std::filesystem::exists(text)&&!std::filesystem::exists(binary),"Dormant logging created output files");
    Check(!Benchmark::GetStatus().active&&!Benchmark::GetStatus().recording&&Benchmark::GetStatus().writtenCalls==0,"Dormant logger collected calls");
    Check(Benchmark::Schedule(3,2,2),"Cannot schedule future diagnostic samples");
    {BenchmarkFrame frame;BenchmarkScope early("Before target frame","Game/Only.cpp",3);}
    Check(!Benchmark::GetStatus().recording&&Benchmark::GetStatus().sampled==0,"Future schedule began collecting too early");
    {
        BenchmarkFrame frame;Check(Benchmark::CurrentFrame()==3&&Benchmark::GetStatus().recording,"Target frame did not enable collection");
        BenchmarkScope sampled("First sample","Game/Only.cpp",4);
    }
    WaitUntil([]{return Benchmark::GetStatus().writtenCalls>=1;},"Worker did not store the first binary sample");
    Check(std::filesystem::exists(binary)&&std::filesystem::file_size(binary)>16,"First sample was not written as binary");
    Check(!std::filesystem::exists(text),"Text was formatted or written before the diagnosis ended");
    {BenchmarkFrame frame;Check(!Benchmark::GetStatus().recording,"Interval gap enabled collection");BenchmarkScope gap("Interval gap","Game/Only.cpp",5);}
    {
        BenchmarkFrame frame;Check(Benchmark::CurrentFrame()==5&&Benchmark::GetStatus().recording,"Final sample did not enable collection");
        BenchmarkScope sampled("Second sample","Game/Only.cpp",6);
    }
    WaitForFinalization();
    const auto beforeSize=std::filesystem::file_size(binary);const auto beforeText=ReadText(text);
    for(int i=0;i<3;++i){BenchmarkFrame frame;BenchmarkScope after("After diagnosis","Game/Only.cpp",7);}
    Check(!Benchmark::GetStatus().recording&&std::filesystem::file_size(binary)==beforeSize&&ReadText(text)==beforeText,"Calls after the diagnosis changed completed logs");
    Check(beforeText.find("First sample")!=std::string::npos&&beforeText.find("Second sample")!=std::string::npos&&
          beforeText.find("Editor idle")==std::string::npos&&beforeText.find("Unarmed game")==std::string::npos&&
          beforeText.find("Before target frame")==std::string::npos&&beforeText.find("Interval gap")==std::string::npos&&beforeText.find("After diagnosis")==std::string::npos,
          "Only requested diagnostic samples should be logged");
    CheckBinaryAndText(binary,text);
}

void NestedUpdate()
{
    RICE_BENCHMARK_FUNCTION();
    std::this_thread::sleep_for(1ms);
}

void CheckBands()
{
    constexpr auto scene = "Engine/Scene/Scene.h";
    constexpr auto physics = "Engine/Scene/Physics.cpp";
    constexpr auto script = "Sandbox/Scripts/Spin.h";
    BenchmarkResult result{{
        {8, 100, {{"Update", scene, 10, 0, 5, 80, 30},
                  {"Step", physics, 12, 1, 15, 20, 20},
                  {"Spin", script, 9, 1, 45, 30, 30}}},
        {11, 60, {{"Spin", script, 9, 0, 10, 50, 50}}}
    }};
    BenchmarkBands time(result, BenchmarkView::Time, BenchmarkGroup::Function);
    Check(time.extent == 160 && time.rows.size() == 2 && time.rows[1].size() == 2, "Time must preserve frame duration and call depth");
    Check(time.rows[0][0].begin == 5 && time.rows[0][0].end == 85 && time.rows[0][1].begin == 110,
          "Timeline lost elapsed time or frame gaps");
    BenchmarkBands scripts(result, BenchmarkView::Time, BenchmarkGroup::Script);
    Check(scripts.rows.size() == 1 && scripts.rows[0].size() == 6, "Script timeline lost a return to the caller");
    Check(scripts.rows[0][1].label == "Physics.cpp" && scripts.rows[0][2].begin == 35, "Wrong script order");

    BenchmarkBands rate(result, BenchmarkView::Rate, BenchmarkGroup::Function);
    Check(rate.extent == 130 && rate.rows.size() == 2, "Rate must use exclusive call time");
    Check(rate.rows[0].size() == 3 && rate.rows[0][0].label == "Spin.h" && rate.rows[0][0].end == 80 &&
          rate.rows[0][1].end == 110 && rate.rows[0][2].end == 130, "Script totals must rank by descending share");
    Check(rate.rows[1][0].label == "Spin" && rate.rows[1][0].logs.size() == 2 &&
          rate.rows[1][0].logs[1].frame == 11, "Aggregate lost individual calls or frame IDs");
    BenchmarkBands features(result, BenchmarkView::Rate, BenchmarkGroup::Feature);
    Check(features.rows[0].size() == 2 && features.rows[0][1].label == "Engine/Scene" &&
          features.rows[0][1].colors.size() == 2, "Folder grouping lost source-file colors");
}

void CheckCapture(const std::filesystem::path& directory)
{
    const auto log = directory / "calls.tsv";
    std::filesystem::remove(log);
    {
        BenchmarkSession session(log);
        Benchmark::BeginPlay();
        {
            BenchmarkFrame frame;
            {
                BenchmarkScope beforeClick("Before diagnosis", "Game/Update.cpp", 7);
                NestedUpdate();
            }
            Check(Benchmark::CurrentFrame() == 1, "First frame must be 1");
            Check(!Benchmark::Schedule(1, 0, 2), "Empty capture accepted");
            Check(!Benchmark::Schedule(1, 3, 0), "Zero interval accepted");
            Check(!Benchmark::Schedule(std::numeric_limits<uint64_t>::max(), 2, 2), "Frame overflow accepted");
            Check(Benchmark::Schedule(Benchmark::CurrentFrame(), 3, 2), "Immediate capture rejected");
            BenchmarkScope afterClick("After diagnosis starts", "Game/Update.cpp", 7);
            NestedUpdate();
        }
        for (int i = 2; i <= 5; ++i) {
            BenchmarkFrame frame;
            BenchmarkScope call(i % 2 ? "Sampled" : "Between samples", "Game/Loop.cpp", 10);
        }
    }

    auto result = Benchmark::Result();
    Check(result && result->frames.size() == 3, "Final frame missing after shutdown");
    for (size_t i = 0; i < result->frames.size(); ++i)
        Check(result->frames[i].number == 1 + 2 * i, "Incorrect frame stride");
    const auto& first = result->frames.front();
    Check(first.calls.size() == 2, "Immediate diagnosis must record only calls after its start");
    const auto& parent = first.calls[0];
    const auto& child = first.calls[1];
    Check(std::string(parent.function) == "After diagnosis starts" && std::string(child.function).find("NestedUpdate") != std::string::npos,
          "Calls must be in entry order");
    Check(parent.depth == 0 && child.depth == 1 && child.line != 0, "Missing nesting or source location");
    Check(parent.startNs <= child.startNs && child.startNs + child.durationNs <= parent.startNs + parent.durationNs,
          "Child lies outside parent");
    Check(child.durationNs > 0 && child.selfNs == child.durationNs && parent.selfNs == parent.durationNs - child.durationNs,
          "Nested time was counted twice");
    Check(parent.startNs + parent.durationNs <= first.durationNs, "Call lies outside frame");

    std::ifstream in(log);
    std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Check(contents.find("Between samples") == std::string::npos && contents.find("Before diagnosis\t") == std::string::npos && contents.find("NestedUpdate") != std::string::npos,
          "The log must contain only calls in the requested diagnostic frames");
    auto binary=log;binary.replace_extension(".bin");CheckBinaryAndText(binary,log);

    // Reusing the recorder must not change an already-published result.
    {
        BenchmarkSession session(directory / "second.tsv");
        Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1, 20, 1), "Initial schedule rejected");
        for (int i = 1; i <= 3; ++i) {
            BenchmarkFrame frame;
            if (i == 2) {
                BenchmarkScope cancelled("Live when cancelled", "Game/Cancel.cpp", 1);
                Benchmark::Cancel();
                {BenchmarkScope afterCancel("After cancellation", "Game/Cancel.cpp", 2);}
                Check(!Benchmark::Schedule(1, 1, 1), "Past frame accepted");
                Check(Benchmark::Schedule(3, 1, 1), "Cannot reschedule after cancelling a partial capture");
            }
        }
    }
    auto second = Benchmark::Result();
    Check(second && second->frames.size() == 1 && second->frames[0].number == 3 && second->frames[0].calls.empty(),
          "Future empty frame was not captured");
    Check(result->frames.size() == 3 && result->frames.back().number == 5, "Published result was modified");
}

void CheckPlayLifecycle(const std::filesystem::path& directory)
{
    BenchmarkSession session(directory / "play.tsv");
    Check(!Benchmark::IsPlaying() && Benchmark::CurrentFrame() == 0, "Editor must start at frame zero");
    Check(Benchmark::Schedule(1, 1, 1), "Cannot arm capture before scene Play");
    for (int i = 0; i < 4; ++i) {
        BenchmarkFrame editorFrame;
        NestedUpdate();
        Check(Benchmark::CurrentFrame() == 0 && Benchmark::GetStatus().sampled == 0,
              "Editor loops advanced the gameplay frame counter");
    }
    {
        BenchmarkFrame editorFrame;
        BenchmarkScope editorCallback("Editor Play callback", "Editor/Panel.cpp", 1);
        Benchmark::BeginPlay();
        Check(Benchmark::IsPlaying() && Benchmark::CurrentFrame() == 0, "Play advanced a partial editor frame");
        NestedUpdate();
    }
    {
        BenchmarkFrame firstGameFrame;
        NestedUpdate();
        Check(Benchmark::CurrentFrame() == 1, "First full Play frame was not frame one");
    }
    Benchmark::EndPlay();
    Check(!Benchmark::IsPlaying() && Benchmark::CurrentFrame() == 0, "Stop did not reset the frame counter");
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!Benchmark::Result() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    auto result = Benchmark::Result();
    Check(result && result->frames.size() == 1 && result->frames.front().calls.size() == 1,
          "Stopping lost a complete capture or captured editor callbacks");
    WaitForFinalization();
    Check(Benchmark::Schedule(2, 20, 1), "Cannot arm a new Play capture");
    Benchmark::BeginPlay();
    {
        BenchmarkFrame firstReplayFrame;
        Check(Benchmark::CurrentFrame() == 1, "Replay did not restart at frame one");
        BenchmarkScope stopCallback("Stop callback", "Editor/Panel.cpp", 2);
        Benchmark::EndPlay();
        NestedUpdate();
    }
    Check(!Benchmark::GetStatus().active && Benchmark::Result() == result, "Stop failed to cancel an incomplete capture");
}

void CheckThreadedCapture(const std::filesystem::path& directory)
{
    std::binary_semaphore entered(0), leave(0);
    {
        BenchmarkSession session(directory / "threads.tsv");
        Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1, 2, 1), "Cannot schedule threaded capture");
        std::thread worker;
        {
            BenchmarkFrame first;
            BenchmarkScope mainCall("Game update", "Game/Loop.cpp", 1);
            worker = std::thread([&] {
                Benchmark::SetThreadName("Background jobs");
                BenchmarkScope outer("Worker outer", "Game/Jobs.cpp", 2);
                BenchmarkScope inner("Worker inner", "Game/Jobs.cpp", 3);
                entered.release();
                leave.acquire();
            });
            entered.acquire();
        }
        {
            BenchmarkFrame second;
            leave.release();
            worker.join();
        }
    }
    auto result = Benchmark::Result();
    Check(result && result->frames.size() == 2 && result->threads.size() == 2, "Missing worker thread capture");
    Check(result->threads[0].id == 0 && result->threads[1].name == "Background jobs", "Thread identity was not preserved");
    uint64_t outerInvocation = 0;
    for (size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
        const auto& frame = result->frames[frameIndex];
        const BenchmarkCall* outer = nullptr;
        const BenchmarkCall* inner = nullptr;
        for (const auto& call : frame.calls) {
            Check(call.startNs >= 0 && call.durationNs >= 0 && call.startNs + call.durationNs <= frame.durationNs,
                  "A cross-frame worker segment escaped its frame");
            if (call.threadId == 0) Check(call.depth == 0, "Game nesting leaked into a worker");
            if (std::string(call.function) == "Worker outer") outer = &call;
            if (std::string(call.function) == "Worker inner") inner = &call;
        }
        Check(outer && inner && outer->threadId == 1 && inner->threadId == 1 && outer->depth == 0 && inner->depth == 1,
              "Worker nesting or source identity is missing");
        Check(outer->selfNs + inner->durationNs == outer->durationNs && inner->selfNs == inner->durationNs,
              "Worker self time counted its nested invocation twice");
        if (frameIndex == 0) outerInvocation = outer->invocationId;
        else Check(outer->invocationId == outerInvocation && outer->continuation && inner->continuation,
                   "Cross-frame continuation lost invocation identity");
    }
    std::binary_semaphore shuttingDown(0), finish(0);
    Benchmark::Initialize(directory / "thread-shutdown.tsv");
    Benchmark::BeginPlay();
    Benchmark::BeginFrame();
    Check(Benchmark::Schedule(1, 1, 1), "Cannot schedule shutdown capture");
    auto worker = std::thread([&] {
        BenchmarkScope live("Live at shutdown", "Game/Jobs.cpp", 4);
        shuttingDown.release();
        finish.acquire();
    });
    shuttingDown.acquire();
    Benchmark::Shutdown();
    finish.release();
    worker.join();
    auto shutdownResult = Benchmark::Result();
    Check(shutdownResult && shutdownResult->frames[0].calls.size() == 1,
          "Shutdown lost a live worker invocation");
    Check(result->threads[1].name == "Background jobs" && result->frames.size() == 2,
          "A subsequent session mutated an immutable threaded result");
    CheckBinaryAndText(directory/"threads.bin",directory/"threads.tsv");
    CheckBinaryAndText(directory/"thread-shutdown.bin",directory/"thread-shutdown.tsv");
}

void CheckThreadedGaps(const std::filesystem::path& directory)
{
    std::binary_semaphore entered(0),doGap(0),gapFinished(0),leave(0);
    {
        BenchmarkSession session(directory/"thread-gaps.tsv");Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1,2,2),"Cannot schedule threaded gap capture");
        std::thread worker;
        {
            BenchmarkFrame frame;
            worker=std::thread([&]{
                Benchmark::SetThreadName("Jobs\twith\nspaces");
                BenchmarkScope outer("Long outer","Game/Jobs.cpp",10),inner("Long inner","Game/Jobs.cpp",11);
                entered.release();doGap.acquire();
                {BenchmarkScope gap("Gap-only child","Game/Jobs.cpp",12);}
                gapFinished.release();leave.acquire();
            });
            entered.acquire();
        }
        {BenchmarkFrame frame;doGap.release();gapFinished.acquire();}
        {BenchmarkFrame frame;leave.release();worker.join();}
    }
    const auto result=Benchmark::Result();Check(result&&result->frames.size()==2,"Threaded gap capture lost frames");
    Check(result->frames[0].number==1&&result->frames[1].number==3,"Threaded capture included an interval gap");
    Check(result->threads.size()==2&&result->threads[1].name=="Jobs with spaces","Thread names were not sanitized consistently");
    const auto& first=result->frames[0].calls;const auto& last=result->frames[1].calls;
    Check(first.size()==2&&last.size()==2,"Scope bookkeeping recorded a gap-only call or lost a continuation");
    for(size_t i=0;i<2;++i){
        Check(last[i].invocationId==first[i].invocationId&&last[i].continuation&&last[i].depth==i,"Sampling gaps changed a live invocation's identity or depth");
        Check(std::string(last[i].function)==std::string(first[i].function),"Sampling gap changed scope metadata");
    }
    Check(last[0].selfNs+last[1].durationNs==last[0].durationNs,"Sampling gap broke exclusive time accounting");
    CheckBinaryAndText(directory/"thread-gaps.bin",directory/"thread-gaps.tsv");
}

void CheckCancellationAndErrors(const std::filesystem::path& directory)
{
    const auto folder=directory/"cancel";std::filesystem::create_directories(folder);
    const auto text=folder/"calls.tsv",binary=folder/"calls.bin";
    {
        BenchmarkSession session(text);Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1,10,1),"Cannot schedule cancellable diagnosis");
        {BenchmarkFrame frame;BenchmarkScope call("Kept cancellation record","Game/Cancel.cpp",1);}
        Benchmark::Cancel();Check(!Benchmark::GetStatus().active&&!Benchmark::GetStatus().recording,"Cancel did not immediately disable collection");
        WaitForFinalization();Check(!Benchmark::Result(),"Cancelled partial diagnosis replaced the published result");
        CheckBinaryAndText(binary,text,true);
        const auto cancelledText=ReadText(text);
        Check(cancelledText.find("Kept cancellation record")!=std::string::npos,"Cancellation discarded completed diagnostic calls");
        Check(Benchmark::Schedule(2,1,1),"Cannot reschedule after cancelled export");
        const auto next=Benchmark::GetStatus();
        {BenchmarkFrame frame;BenchmarkScope call("Fresh diagnosis","Game/Cancel.cpp",2);}
        WaitForFinalization();CheckBinaryAndText(next.binaryFile,next.textFile);
        Check(ReadText(next.textFile).find("Fresh diagnosis")!=std::string::npos&&ReadText(next.textFile).find("Kept cancellation record")==std::string::npos,"New diagnosis reused cancelled calls");
        Check(next.textFile!=text.string()&&ReadText(text)==cancelledText,"Starting a new diagnosis discarded the previous converted log");
    }
    const auto blocked=directory/"not-a-directory";{std::ofstream file(blocked);file<<"file";}
    {
        BenchmarkSession session(blocked/"calls.tsv");Benchmark::BeginPlay();
        Check(Benchmark::GetStatus().logError.empty(),"Dormant initialization performed unexpected file I/O");
        Check(Benchmark::Schedule(1,1,1),"Cannot schedule diagnosis with a failing output path");
        {BenchmarkFrame frame;BenchmarkScope call("Survives log failure","Game/Error.cpp",1);}
        WaitForFinalization();
        Check(!Benchmark::GetStatus().logError.empty(),"Binary write failure was not reported");
        auto result=Benchmark::Result();Check(result&&result->frames.size()==1&&result->frames[0].calls.size()==1,"Log I/O failure discarded the in-memory diagnostic result");
    }
    const auto occupied=directory/"folder.tsv";std::filesystem::create_directories(occupied);
    {std::ofstream marker(occupied/"keep.txt");marker<<"preserved";}
    {
        BenchmarkSession session(occupied);Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1,1,1),"Cannot schedule occupied output path test");
        {BenchmarkFrame frame;BenchmarkScope call("Occupied path","Game/Error.cpp",2);}
        WaitForFinalization();
        Check(!Benchmark::GetStatus().logError.empty()&&std::filesystem::is_directory(occupied)&&ReadText(occupied/"keep.txt")=="preserved",
              "Log rotation must reject a directory without moving its contents");
    }
}

void CheckImmediateReschedule(const std::filesystem::path& directory)
{
    BenchmarkStatus oldCapture,newCapture;
    {
        BenchmarkSession session(directory/"reschedule.tsv");Benchmark::BeginPlay();
        Check(Benchmark::Schedule(1,10,1),"Cannot schedule diagnosis before immediate rescheduling");
        {
            BenchmarkFrame frame;
            for(int i=0;i<20000;++i){BenchmarkScope call("Old cancelled capture only","Game/Reschedule.cpp",1);}
        }
        WaitUntil([]{return Benchmark::GetStatus().writtenCalls==20000;},"Initial reschedule binary did not drain");
        oldCapture=Benchmark::GetStatus();
        Benchmark::Cancel();
        Check(Benchmark::Schedule(2,1,1),"A cancelled capture blocked immediate rescheduling");
        newCapture=Benchmark::GetStatus();
        Check(oldCapture.binaryFile!=newCapture.binaryFile&&oldCapture.textFile!=newCapture.textFile,"Concurrent finalization reused another capture's output paths");
        {BenchmarkFrame frame;BenchmarkScope call("New capture only","Game/Reschedule.cpp",2);}
        // Shutdown joins the exporter for both captures, including the cancelled
        // one whose text conversion was deferred while the next diagnosis ran.
    }
    CheckBinaryAndText(oldCapture.binaryFile,oldCapture.textFile,true);
    CheckBinaryAndText(newCapture.binaryFile,newCapture.textFile);
    const auto oldText=ReadText(oldCapture.textFile),newText=ReadText(newCapture.textFile);
    Check(oldText.find("Old cancelled capture only")!=std::string::npos&&oldText.find("New capture only")==std::string::npos,
          "Deferred old converter read the newer diagnosis binary");
    Check(newText.find("New capture only")!=std::string::npos&&newText.find("Old cancelled capture only")==std::string::npos,
          "New diagnosis output included cancelled capture records");
    const auto result=Benchmark::Result();
    Check(result&&result->frames.size()==1&&result->frames[0].number==2&&result->frames[0].calls.size()==1,
          "Cancelled converter replaced the newly published diagnosis result");
}
}

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "Expected a test output directory");
        auto directory = std::filesystem::path(argv[1]) / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        CheckBands();
        CheckDiagnosticOnly(directory);
        CheckCapture(directory);
        CheckPlayLifecycle(directory);
        CheckThreadedCapture(directory);
        CheckThreadedGaps(directory);
        CheckCancellationAndErrors(directory);
        CheckImmediateReschedule(directory);
        std::cout << "Function benchmark passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
