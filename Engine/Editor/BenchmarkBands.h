#pragma once

#include "Rice/Benchmark.h"
#include <algorithm>
#include <map>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Rice {

enum class BenchmarkView { Time, Rate };
enum class BenchmarkGroup { Function, Script, Feature };

struct BenchmarkLogRef {
    uint64_t frame;
    const BenchmarkCall* call;
};

struct BenchmarkColorSpan {
    std::string_view file;
    double end;
};

struct BenchmarkBand {
    double begin, end;
    std::string_view label, file;
    std::span<const BenchmarkLogRef> logs;
    std::vector<BenchmarkColorSpan> colors;
};

struct BenchmarkThreadBands {
    uint64_t id;
    std::string name;
    size_t firstRow = 0, rowCount = 0, calls = 0;
    int64_t selfNs = 0;
};

inline std::string_view BenchmarkPath(std::string_view path) {
    for (auto root : {"/Engine/", "\\Engine\\", "/Sandbox/", "\\Sandbox\\"}) {
        auto pos = path.rfind(root);
        if (pos != path.npos) return path.substr(pos + 1);
    }
    return path;
}

inline std::string_view BenchmarkFileName(std::string_view path) {
    return path.substr(path.find_last_of("/\\") + 1);
}

inline std::string_view BenchmarkFolder(std::string_view path) {
    path = BenchmarkPath(path);
    auto pos = path.find_last_of("/\\");
    return pos == path.npos ? "." : path.substr(0, pos);
}

// The caller retains the immutable result while displaying these log references.
class BenchmarkBands {
public:
    using Row = std::vector<BenchmarkBand>;
    std::vector<Row> rows;
    std::vector<BenchmarkThreadBands> threads;
    std::vector<Row> compactRows;
    double extent = 0;

    BenchmarkBands(const BenchmarkResult& result, BenchmarkView view, BenchmarkGroup group) {
        std::map<uint64_t, std::string> names;
        size_t calls = 0;
        for (const auto& thread : result.threads) names[thread.id] = thread.name;
        for (const auto& frame : result.frames) {
            calls += frame.calls.size();
            for (const auto& call : frame.calls) names.try_emplace(call.threadId);
        }
        if (names.empty()) names.emplace(0, "Game");
        // Thread slices and compact subtree logs retain spans into these buffers.
        timeLogs.reserve(group == BenchmarkGroup::Function ? calls : 2 * calls);
        compactLogs.reserve(calls);
        for (auto& [id, name] : names) {
            if (name.empty()) name = id == 0 ? "Game" : "Worker " + std::to_string(id);
            auto& thread = threads.emplace_back(BenchmarkThreadBands{id, std::move(name), rows.size()});
            compactRows.emplace_back();
            for (const auto& frame : result.frames)
                for (const auto& call : frame.calls) if (call.threadId == id) {
                    ++thread.calls;
                    thread.selfNs += call.selfNs;
                }
            if (view == BenchmarkView::Rate) BuildRate(result, group, thread);
            else BuildTime(result, group, thread);
            thread.rowCount = rows.size() - thread.firstRow;
        }
    }
    BenchmarkBands(const BenchmarkBands&) = delete;
    BenchmarkBands& operator=(const BenchmarkBands&) = delete;

private:
    std::unordered_map<const char*, std::string> sourcePaths;
    std::vector<BenchmarkLogRef> timeLogs;
    std::vector<BenchmarkLogRef> compactLogs;
    std::vector<std::vector<BenchmarkLogRef>> rateLogs;

    std::string_view File(const char* raw) {
        auto [it, inserted] = sourcePaths.try_emplace(raw);
        if (inserted) {
            std::string path(raw);
            std::replace(path.begin(), path.end(), '\\', '/');
            auto normalized = std::filesystem::path(std::u8string(path.begin(), path.end())).lexically_normal().generic_u8string();
            path.assign(normalized.begin(), normalized.end());
            it->second = BenchmarkPath(path);
        }
        return it->second;
    }

    static void AddColor(BenchmarkBand& band, std::string_view file, double begin, double end) {
        if (band.file.empty()) band.file = file;
        if (band.colors.empty()) {
            if (band.file == file) return;
            band.colors.push_back({band.file, begin});
        }
        if (band.colors.back().file == file) band.colors.back().end = end;
        else band.colors.push_back({file, end});
    }

    void AddSlice(Row& row, BenchmarkLogRef log, double begin, double end, BenchmarkGroup group) {
        if (end <= begin) return;
        auto file = File(log.call->file);
        bool feature = group == BenchmarkGroup::Feature;
        auto label = feature ? BenchmarkFolder(file) : BenchmarkFileName(file);
        if (row.empty() || row.back().end != begin || row.back().logs.front().frame != log.frame ||
            (feature ? row.back().label != label : row.back().file != file)) {
            row.push_back({begin, end, label, file, {}, {}});
        }
        auto& band = row.back();
        band.end = end;
        timeLogs.push_back(log);
        band.logs = std::span(timeLogs).last(band.logs.size() + 1);
        if (feature) AddColor(band, file, begin, end);
    }

    void BuildTime(const BenchmarkResult& result, BenchmarkGroup group, const BenchmarkThreadBands& thread) {
        rows.emplace_back();
        auto& compact = compactRows.back();
        double offset = 0;
        std::vector<const BenchmarkCall*> stack;
        for (const auto& frame : result.frames) {
            int64_t cursor = 0;
            const BenchmarkCall* root = nullptr;
            auto closeScope = [&] {
                auto* call = stack.back();
                stack.pop_back();
                int64_t end = call->startNs + call->durationNs;
                AddSlice(rows[thread.firstRow], {frame.number, call}, offset + cursor, offset + end, group);
                cursor = end;
            };
            for (const auto& call : frame.calls) {
                if (call.threadId != thread.id) continue;
                if (!root || call.depth <= root->depth) {
                    root = &call;
                    auto file = File(call.file);
                    auto label = group == BenchmarkGroup::Function ? std::string_view(call.function) :
                        group == BenchmarkGroup::Script ? BenchmarkFileName(file) : BenchmarkFolder(file);
                    compact.push_back({offset + call.startNs, offset + call.startNs + call.durationNs, label, file, {}, {}});
                }
                compactLogs.push_back({frame.number, &call});
                compact.back().logs = std::span(compactLogs).last(compact.back().logs.size() + 1);
                if (group == BenchmarkGroup::Function) {
                    size_t row = thread.firstRow + call.depth;
                    if (rows.size() <= row) rows.resize(row + 1);
                    double begin = offset + call.startNs;
                    timeLogs.push_back({frame.number, &call});
                    rows[row].push_back({begin, begin + call.durationNs, call.function, File(call.file),
                                               std::span(timeLogs).last(1), {}});
                } else {
                    while (!stack.empty() && stack.back()->depth >= call.depth) closeScope();
                    if (!stack.empty())
                        AddSlice(rows[thread.firstRow], {frame.number, stack.back()}, offset + cursor, offset + call.startNs, group);
                    cursor = call.startNs;
                    stack.push_back(&call);
                }
            }
            while (!stack.empty()) closeScope();
            offset += frame.durationNs;
        }
        extent = offset;
    }

    struct Total {
        int64_t ns = 0;
        std::vector<BenchmarkLogRef> logs;
        void Add(BenchmarkLogRef log) {
            ns += log.call->selfNs;
            logs.push_back(log);
        }
    };

    template<typename Map>
    static auto Ranked(Map& map) {
        std::vector<typename Map::iterator> ranked;
        for (auto it = map.begin(); it != map.end(); ++it) ranked.push_back(it);
        // Map order breaks ties consistently across captures.
        std::stable_sort(ranked.begin(), ranked.end(), [](auto a, auto b) {
            return a->second.ns > b->second.ns;
        });
        return ranked;
    }

    void BuildRate(const BenchmarkResult& result, BenchmarkGroup group, const BenchmarkThreadBands& thread) {
        const bool feature = group == BenchmarkGroup::Feature, functions = group == BenchmarkGroup::Function;
        struct GroupTotal : Total {
            std::map<std::pair<std::string_view, uint32_t>, Total> children;
        };
        std::map<std::string_view, GroupTotal> totals;
        std::map<std::string_view, int64_t> fileTimes;
        size_t firstLog = compactLogs.size();
        for (const auto& frame : result.frames) {
            for (const auto& call : frame.calls) {
                if (call.threadId != thread.id) continue;
                auto file = File(call.file);
                BenchmarkLogRef log{frame.number, &call};
                compactLogs.push_back(log);
                fileTimes[file] += call.selfNs;
                auto& top = totals[feature ? BenchmarkFolder(file) : file];
                top.Add(log);
                if (functions) top.children[{call.function, call.line}].Add(log);
                if (feature) top.children[{file, 0}].ns += call.selfNs;
            }
        }
        rows.resize(rows.size() + (functions ? 2 : 1));
        double offset = 0;
        for (auto entry : Ranked(totals)) {
            auto& [key, total] = *entry;
            if (total.ns <= 0) continue;
            auto& top = rows[thread.firstRow].emplace_back(BenchmarkBand{offset, offset + total.ns,
                feature ? key : BenchmarkFileName(key), feature ? "" : key, rateLogs.emplace_back(std::move(total.logs)), {}});
            double x = offset;
            for (auto child : Ranked(total.children)) {
                auto& [childKey, childTotal] = *child;
                if (childTotal.ns <= 0) continue;
                double end = x + childTotal.ns;
                if (feature) AddColor(top, childKey.first, x, end);
                else rows[thread.firstRow + 1].push_back({x, end, childKey.first, key, rateLogs.emplace_back(std::move(childTotal.logs)), {}});
                x = end;
            }
            offset = top.end;
        }
        extent += offset;
        auto& compact = compactRows.back().emplace_back(BenchmarkBand{0, offset, "Thread total", "",
            std::span(compactLogs).subspan(firstLog), {}});
        std::vector<std::pair<std::string_view, int64_t>> rankedFiles(fileTimes.begin(), fileTimes.end());
        std::stable_sort(rankedFiles.begin(), rankedFiles.end(), [](auto a, auto b) { return a.second > b.second; });
        double x = 0;
        for (auto [file, ns] : rankedFiles) {
            AddColor(compact, file, x, x + ns);
            x += ns;
        }
    }
};

} // namespace Rice
