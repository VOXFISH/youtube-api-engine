#include "Rice/Benchmark.h"
#include "Rice/Debug.h"
#include <spdlog/sinks/base_sink.h>
#include <algorithm>
#include <deque>
#include <mutex>
namespace Rice {
namespace {
struct MessageStore {
    std::mutex mutex;
    std::deque<LogEntry> entries;
    uint64_t sequence=0;
};
MessageStore& Messages(){ RICE_BENCHMARK_FUNCTION(); static MessageStore store;return store;}
class EditorSink final:public spdlog::sinks::base_sink<std::mutex> {
    void sink_it_(const spdlog::details::log_msg& message) override {
        RICE_BENCHMARK_FUNCTION();
        spdlog::memory_buf_t formatted;
        formatter_->format(message,formatted);
        // Bound both entry count and individual message size.
        std::string text(formatted.data(),std::min<size_t>(formatted.size(),16384));
        if(formatted.size()>16384)text+=" ... [truncated]";
        while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();
        auto& store=Messages();std::lock_guard lock(store.mutex);
        store.entries.push_back({++store.sequence,message.level,std::move(text)});
        while(store.entries.size()>Debug::MessageCapacity)store.entries.pop_front();
    }
    void flush_() override { RICE_BENCHMARK_FUNCTION(); }
};
}
Ref<spdlog::logger> Debug::s_CoreLogger;
Ref<spdlog::logger> Debug::s_ClientLogger;
void Debug::Init(){
    RICE_BENCHMARK_FUNCTION();
    static std::once_flag once;
    std::call_once(once,[]{
        RICE_BENCHMARK_FUNCTION();
        auto sink=std::make_shared<EditorSink>();
        sink->set_pattern("[%T] [%l] %n: %v");
        s_CoreLogger=std::make_shared<spdlog::logger>("Rice",sink);
        s_ClientLogger=std::make_shared<spdlog::logger>("APP",sink);
        s_CoreLogger->set_level(spdlog::level::trace);s_ClientLogger->set_level(spdlog::level::trace);
        spdlog::register_logger(s_CoreLogger);spdlog::register_logger(s_ClientLogger);
        spdlog::set_default_logger(s_ClientLogger);
    });
}
std::vector<LogEntry> Debug::GetMessages(){ RICE_BENCHMARK_FUNCTION(); auto& store=Messages();std::lock_guard lock(store.mutex);return {store.entries.begin(),store.entries.end()};}
void Debug::ClearMessages(){ RICE_BENCHMARK_FUNCTION(); auto& store=Messages();std::lock_guard lock(store.mutex);store.entries.clear();}
}
