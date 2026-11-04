#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Rice {

struct BenchmarkCall {
    const char* function;
    const char* file;
    uint32_t line, depth;
    int64_t startNs, durationNs, selfNs;
    uint64_t threadId = 0, invocationId = 0;
    bool continuation = false;
};

struct BenchmarkFrameData {
    uint64_t number = 0;
    int64_t durationNs = 0;
    std::vector<BenchmarkCall> calls; // In entry order, interleaved by start time across threads.
};

struct BenchmarkResult {
    std::vector<BenchmarkFrameData> frames;
    struct ThreadInfo {
        uint64_t id;
        std::string name;
    };
    std::vector<ThreadInfo> threads;
};
using BenchmarkThreadInfo = BenchmarkResult::ThreadInfo;

struct BenchmarkStatus {
    bool active = false;
    bool recording = false, finalizing = false;
    uint32_t sampled = 0, requested = 0;
    uint64_t writtenCalls = 0, binaryBytes = 0;
    std::string binaryFile, textFile;
    std::string logError;
    bool playing = false;
};

class Benchmark {
public:
    // Session and frame lifecycle calls belong to the game thread.
    static void Initialize(std::filesystem::path logFile);
    static void Shutdown();
    // A Play request made inside editor UI starts counting at the next full frame.
    static void BeginPlay();
    static void EndPlay();
    static bool IsPlaying();
    // Call from the thread being named. Other threads are registered on first use.
    static void SetThreadName(std::string name);
    static void BeginFrame();
    static void EndFrame();
    static uint64_t CurrentFrame();

    // Play frames start at 1. Scheduling while editing arms the next Play session.
    // An interval of 1 captures consecutive frames. Only selected frames record
    // calls. Scheduling the current frame starts at the request, not retroactively.
    static bool Schedule(uint64_t start, uint32_t frames, uint32_t intervals);
    static void Cancel();
    static BenchmarkStatus GetStatus();
    static std::shared_ptr<const BenchmarkResult> Result();
};

struct BenchmarkThreadRecorder;

class BenchmarkSession {
public:
    explicit BenchmarkSession(const std::filesystem::path& file) { Benchmark::Initialize(file); }
    ~BenchmarkSession() { Benchmark::Shutdown(); }
    BenchmarkSession(const BenchmarkSession&) = delete;
    BenchmarkSession& operator=(const BenchmarkSession&) = delete;
};

class BenchmarkFrame {
public:
    BenchmarkFrame() { Benchmark::BeginFrame(); }
    ~BenchmarkFrame() { Benchmark::EndFrame(); }
    BenchmarkFrame(const BenchmarkFrame&) = delete;
    BenchmarkFrame& operator=(const BenchmarkFrame&) = delete;
};

class BenchmarkScope {
public:
    // Metadata must have static storage duration, including for published results.
    BenchmarkScope(const char* function, const char* file, uint32_t line = 0);
    ~BenchmarkScope();
    BenchmarkScope(const BenchmarkScope&) = delete;
    BenchmarkScope& operator=(const BenchmarkScope&) = delete;
private:
    friend class Benchmark;
    friend struct BenchmarkThreadRecorder;
    void Finish(int64_t endNs);
    void BeginSegment(int64_t startNs, bool continuation);
    size_t index = 0;
    BenchmarkScope* parent = nullptr;
    std::shared_ptr<BenchmarkThreadRecorder> recorder;
    const char* function = nullptr;
    const char* file = nullptr;
    uint32_t line = 0;
    uint64_t invocationId = 0;
    bool active = false;
};

} // namespace Rice

#define RICE_BENCHMARK_JOIN_(a, b) a##b
#define RICE_BENCHMARK_JOIN(a, b) RICE_BENCHMARK_JOIN_(a, b)
#define RICE_BENCHMARK_SCOPE(name) \
    ::Rice::BenchmarkScope RICE_BENCHMARK_JOIN(riceBenchmarkScope_, __LINE__){name, __FILE__, __LINE__}
#ifdef _MSC_VER
#define RICE_BENCHMARK_FUNCTION() RICE_BENCHMARK_SCOPE(__FUNCTION__)
#else
#define RICE_BENCHMARK_FUNCTION() RICE_BENCHMARK_SCOPE(__func__)
#endif
