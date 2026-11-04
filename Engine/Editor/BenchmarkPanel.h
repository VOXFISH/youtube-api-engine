#pragma once

#include "BenchmarkBands.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Rice {

class BenchmarkPanel {
    uint64_t start = 1;
    uint32_t frames = 1, intervals = 1;
    bool followFrame = true, scheduleFailed = false;
    BenchmarkView view = BenchmarkView::Time;
    BenchmarkGroup timeGroup = BenchmarkGroup::Function, rateGroup = BenchmarkGroup::Script;
    std::shared_ptr<const BenchmarkResult> result;
    std::unique_ptr<BenchmarkBands> bands;
    std::unordered_set<uint64_t> collapsedThreads;
    double zoom = 1, left = 0; // Fraction of the entire graph at the left edge.
    struct Range {
        size_t row, first, last; // Inclusive indices in a single row.
        bool operator==(const Range&) const = default;
    };
    std::optional<Range> selection;
    ImVec2 selectionMin{}, selectionMax{};
    std::vector<BenchmarkLogRef> selectedLogs;
    double logScroll = 0;
    // Keep collision-resolved colors when the mode or capture changes.
    std::map<std::string, ImU32, std::less<>> fileColors;
    std::unordered_set<ImU32> usedColors;
    std::vector<Range> layout;
    std::unordered_map<std::string_view, float> textWidths;
    double layoutFrom = -1, layoutTo = -1;
    float layoutWidth = 0, fontSize = 0;
    ImFont* font = nullptr;

    BenchmarkGroup& Group() { return view == BenchmarkView::Time ? timeGroup : rateGroup; }

    void Fit() {
        left = 0;
        zoom = 1;
        selection.reset();
    }

    void ResetModel() {
        layout.clear();
        layoutFrom = -1;
        textWidths.clear();
        std::vector<BenchmarkLogRef>().swap(selectedLogs);
        bands.reset();
        Fit();
    }

    // Wrap compact controls when the window occupies a narrow dock.
    static void NextControl(float width) {
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
            ImGui::SameLine();
    }

    void DrawSettings() {
        uint64_t current = Benchmark::CurrentFrame();
        auto status = Benchmark::GetStatus();
        float em = ImGui::GetFontSize();
        if (followFrame) start = std::max(uint64_t(1), current);
        ImGui::BeginDisabled(status.active || status.finalizing);
        ImGui::SetNextItemWidth(em * 5.3f);
        ImGui::InputScalar("Start", ImGuiDataType_U64, &start);
        if (ImGui::IsItemActive()) followFrame = false;
        ImGui::SetItemTooltip("First frame to sample. Schedule a future frame to capture it in full.");
        NextControl(em * 7);
        ImGui::SetNextItemWidth(em * 3.3f);
        ImGui::InputScalar("Frames", ImGuiDataType_U32, &frames);
        ImGui::SetItemTooltip("Number of frames to sample.");
        NextControl(em * 8);
        ImGui::SetNextItemWidth(em * 3.3f);
        ImGui::InputScalar("Intervals", ImGuiDataType_U32, &intervals);
        ImGui::SetItemTooltip("Distance between sampled frame numbers. Use 1 for consecutive frames.");
        NextControl(em * 4.9f);
        bool scheduled = ImGui::Button("Schedule");
        ImGui::SetItemTooltip("Start sampling at this scene frame. Frame 1 follows scene Play.");
        NextControl(em * 3.1f);
        ImGui::BeginDisabled(!status.playing || current == 0);
        bool play = ImGui::Button("Play");
        ImGui::SetItemTooltip("Start at this scene frame, recording calls after this click. Schedule a future Start for a full first frame. Requires scene Play.");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (play || scheduled) {
            if (play) start = current;
            followFrame = false;
            scheduleFailed = !Benchmark::Schedule(start, frames, intervals);
            status = Benchmark::GetStatus();
        }
        if (!status.logError.empty()) ImGui::TextWrapped("%s", status.logError.c_str());
        if (scheduleFailed)
            ImGui::TextWrapped("Check the frame range. Use a current or future start and positive counts.");
        NextControl(em * 7.2f);
        ImGui::TextDisabled("Frame #%llu", static_cast<unsigned long long>(current));
        if (!status.playing) {
            NextControl(em * 10);
            ImGui::TextDisabled("Edit mode");
        }
        if (status.finalizing) {
            NextControl(em * 15);
            ImGui::Text("Exporting log... %llu calls", static_cast<unsigned long long>(status.writtenCalls));
        } else if (status.active) {
            NextControl(em * 10);
            ImGui::Text("%s %u / %u", status.recording ? "Capturing" : "Waiting", status.sampled, status.requested);
            NextControl(em * 5);
            if (ImGui::SmallButton("Cancel")) Benchmark::Cancel();
        } else {
            NextControl(em * 8);
            ImGui::TextDisabled("Logging off");
            if (status.writtenCalls && status.logError.empty()) {
                NextControl(em * 8);
                ImGui::TextDisabled("Log ready");
                ImGui::SetItemTooltip("Text: %s\nBinary: %s", status.textFile.c_str(), status.binaryFile.c_str());
            }
        }
    }

    void DrawModes() {
        float em = ImGui::GetFontSize();
        auto modeButton = [&](const char* name, BenchmarkView target) {
            if (ImGui::RadioButton(name, view == target) && view != target) {
                view = target;
                ResetModel();
            }
        };
        auto groupButton = [&](const char* name, BenchmarkGroup target) {
            if (ImGui::RadioButton(name, Group() == target) && Group() != target) {
                Group() = target;
                ResetModel();
            }
        };
        modeButton("Time", BenchmarkView::Time);
        NextControl(em * 4); modeButton("Rate", BenchmarkView::Rate);
        NextControl(em * 5.1f); groupButton("Script", BenchmarkGroup::Script);
        NextControl(em * 5.5f); groupButton("Feature", BenchmarkGroup::Feature);
        NextControl(em * 6.3f); groupButton("Function", BenchmarkGroup::Function);
        NextControl(em * 2.7f);
        if (ImGui::SmallButton("Fit")) Fit();
        ImGui::SetItemTooltip("Show the full capture.");
    }

    ImU32 Color(std::string_view file) {
        auto it = fileColors.find(file);
        if (it != fileColors.end()) return it->second;
        uint32_t hash = 2166136261u;
        for (unsigned char c : file) hash = (hash ^ c) * 16777619u;
        ImU32 color;
        do {
            float hue = float(hash & 0xffffu) / 65536.f;
            float sat = .54f + .2f * float((hash >> 16) & 255u) / 255.f;
            color = ImGui::ColorConvertFloat4ToU32(ImColor::HSV(hue, sat, .68f));
            hash = hash * 1664525u + 1013904223u;
        } while (usedColors.contains(color));
        usedColors.insert(color);
        fileColors.emplace(file, color);
        return color;
    }

    float LabelWidth(std::string_view label) {
        auto [it, inserted] = textWidths.try_emplace(label, 0.f);
        if (inserted) it->second = ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + 8.f;
        return it->second;
    }

    const BenchmarkBands::Row& Row(size_t index) const {
        return index < bands->rows.size() ? bands->rows[index] : bands->compactRows[index - bands->rows.size()];
    }

    void Layout(double from, double to, float width) {
        if (font != ImGui::GetFont() || fontSize != ImGui::GetFontSize()) {
            font = ImGui::GetFont();
            fontSize = ImGui::GetFontSize();
            textWidths.clear();
            layoutFrom = -1;
        }
        if (layoutFrom == from && layoutTo == to && layoutWidth == width) return;
        layout.clear();
        const double px = width / (to - from);
        const float miscWidth = LabelWidth("misc.");
        auto narrow = [&](const BenchmarkBand& b) { return (b.end - b.begin) * px < LabelWidth(b.label); };
        for (size_t r = 0; r < bands->rows.size() + bands->compactRows.size(); ++r) {
            const auto& row = Row(r);
            auto first = std::lower_bound(row.begin(), row.end(), from,
                [](const auto& b, double x) { return b.end <= x; });
            auto stop = std::lower_bound(first, row.end(), to,
                [](const auto& b, double x) { return b.begin < x; });
            // Work inward from the small end of a ranked row. Keep gaps that occupy a visible pixel.
            while (stop != first) {
                auto last = --stop;
                if (narrow(*last)) {
                    while (stop != first && (last->end - stop->begin) * px < miscWidth) {
                        auto previous = stop - 1;
                        if ((stop->begin - previous->end) * px > 1 || !narrow(*previous)) break;
                        stop = previous;
                    }
                }
                layout.push_back({r, size_t(stop - row.begin()), size_t(last - row.begin())});
            }
        }
        layoutFrom = from;
        layoutTo = to;
        layoutWidth = width;
    }

    void Select(Range range) {
        selection = range;
        selectedLogs.clear();
        logScroll = 0;
        const auto& row = Row(range.row);
        for (size_t i = range.first; i <= range.last; ++i)
            selectedLogs.insert(selectedLogs.end(), row[i].logs.begin(), row[i].logs.end());
        // A parent can yield several self-time slices around its children.
        std::sort(selectedLogs.begin(), selectedLogs.end(), [](const auto& a, const auto& b) {
            if (a.frame != b.frame) return a.frame < b.frame;
            if (a.call->startNs != b.call->startNs) return a.call->startNs < b.call->startNs;
            return a.call < b.call; // Same frame, same immutable calls array.
        });
        selectedLogs.erase(std::unique(selectedLogs.begin(), selectedLogs.end(),
            [](auto a, auto b) { return a.call == b.call; }), selectedLogs.end());
    }

    size_t VisibleLogs() const {
        float h = ImGui::GetWindowViewport()->WorkSize.y;
        return std::min(selectedLogs.size(), size_t(std::clamp(h / (ImGui::GetTextLineHeightWithSpacing() * 8.f), 1.f, 5.f)));
    }

    void DrawLogs(float wheel) {
        if (selectedLogs.empty() || !ImGui::BeginTooltip()) return;
        const auto& range = *selection;
        const auto& row = Row(range.row);
        auto label = range.first == range.last ? row[range.first].label : std::string_view("misc.");
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        double span = row[range.last].end - row[range.first].begin;
        if (view == BenchmarkView::Rate)
            ImGui::Text("%.3f%% of measured time   |   %.3f us self", 100 * span / bands->extent, span / 1e3);
        else ImGui::Text("Time range   %.3f - %.3f ms", row[range.first].begin / 1e6, row[range.last].end / 1e6);
        auto available = ImGui::GetWindowViewport()->WorkSize;
        size_t count = VisibleLogs();
        logScroll = std::clamp(logScroll - double(wheel) * 2, 0.0, double(selectedLogs.size() - count));
        size_t first = size_t(logScroll);
        ImGui::TextDisabled("Calls %zu - %zu of %zu%s", first + 1, first + count, selectedLogs.size(),
                            count < selectedLogs.size() ? "   |   Wheel to scroll" : "");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(available.x * .85f, ImGui::GetFontSize() * 43));
        for (size_t i = first; i < first + count; ++i) {
            const auto& [frame, call] = selectedLogs[i];
            ImGui::Separator();
            ImGui::TextUnformatted(call->function);
            auto file = BenchmarkPath(call->file);
            ImGui::Text("%.*s(%u)", int(file.size()), file.data(), call->line);
            ImGui::Text("Frame %llu   Thread #%llu   +%.3f ms", static_cast<unsigned long long>(frame),
                        static_cast<unsigned long long>(call->threadId), call->startNs / 1e6);
            ImGui::Text("%.3f us elapsed   %.3f us self", call->durationNs / 1e3, call->selfNs / 1e3);
            if (call->continuation)
                ImGui::TextDisabled("Continued invocation #%llu", static_cast<unsigned long long>(call->invocationId));
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    bool DrawThreadGraph(size_t firstRow, size_t rowCount) {
        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
        const float rowH = ImGui::GetTextLineHeight() + 9.f;
        auto origin = ImGui::GetCursorScreenPos();
        float height = rowH * (float(rowCount) + 1);
        ImGui::InvisibleButton("##BenchmarkBands", {width, height},
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        bool hovered = ImGui::IsItemHovered();
        auto& io = ImGui::GetIO();
        bool reading = hovered && selection && selectedLogs.size() > VisibleLogs()
                    && ImGui::IsMouseHoveringRect(selectionMin, selectionMax);
        double anchor = std::clamp(double(io.MousePos.x - origin.x) / width, 0.0, 1.0);
        double steps = hovered && !reading ? io.MouseWheel : 0;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            steps -= io.MouseDelta.y * .035;
        if (steps != 0) {
            double at = left + anchor / zoom;
            // Leave enough room to inspect even the shortest measured event.
            zoom = std::clamp(zoom * std::pow(1.35, steps), 1.0, std::max(1.0, bands->extent));
            left = at - anchor / zoom;
            selection.reset();
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            left -= io.MouseDelta.x / (width * zoom);
            selection.reset();
        }
        left = std::clamp(left, 0.0, 1.0 - 1.0 / zoom);
        double from = left * bands->extent, to = from + bands->extent / zoom;
        auto xpos = [&](double t) {
            return origin.x + float(std::clamp((t - from) / (to - from), 0.0, 1.0) * width);
        };
        Layout(from, to, width);
        auto* draw = ImGui::GetWindowDrawList();
        auto axisColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        auto textColor = IM_COL32(255, 255, 255, 255);
        ImVec2 bottomRight{origin.x + width, origin.y + height};
        draw->PushClipRect(origin, bottomRight, true);
        draw->AddRectFilled({origin.x, origin.y + rowH}, bottomRight, ImGui::GetColorU32(ImGuiCol_FrameBg));
        int ticks = width < ImGui::GetFontSize() * 30 ? 2 : 4;
        for (int tick = 0; tick <= ticks; ++tick) {
            double t = from + (to - from) * tick / ticks;
            char label[40];
            if (view == BenchmarkView::Rate) {
                int decimals = std::clamp(1 - int(std::floor(std::log10(100.0 / (zoom * ticks)))), 0, 12);
                std::snprintf(label, sizeof(label), "%.*f%%", decimals, 100 * t / bands->extent);
            }
            else if ((to - from) < 1000)
                std::snprintf(label, sizeof(label), "%.0f ns", t);
            else if ((to - from) < 1000000)
                std::snprintf(label, sizeof(label), "%.5f us", t / 1e3);
            else
                std::snprintf(label, sizeof(label), "%.3f ms", t / 1e6);
            float x = xpos(t);
            float textX = x - ImGui::CalcTextSize(label).x * (float(tick) / ticks);
            draw->AddText({textX, origin.y}, axisColor, label);
            draw->AddLine({x, origin.y + rowH - 5}, {x, bottomRight.y}, ImGui::GetColorU32(ImGuiCol_Border));
        }
        bool selectedHovered = false;
        for (const auto& range : layout) {
            if (range.row < firstRow || range.row >= firstRow + rowCount) continue;
            const auto& row = Row(range.row);
            const auto& band = row[range.first];
            bool misc = range.first != range.last;
            ImVec2 lo{xpos(band.begin), origin.y + rowH * (float(range.row - firstRow) + 1)};
            ImVec2 hi{xpos(row[range.last].end), lo.y + rowH - 2};
            if (hi.x <= lo.x) continue;
            ImU32 base = misc ? IM_COL32(92, 96, 105, 255) :
                Color(band.colors.empty() ? band.file : band.colors.front().file);
            draw->AddRectFilled(lo, hi, base);
            if (!misc && !band.colors.empty()) {
                auto it = std::lower_bound(band.colors.begin(), band.colors.end(), from,
                    [](const auto& c, double x) { return c.end <= x; });
                float x = lo.x;
                for (; it != band.colors.end() && x < hi.x; ++it) {
                    float next = xpos(it->end);
                    if (std::floor(next) > std::floor(x))
                        draw->AddRectFilled({x, lo.y}, {next, hi.y}, Color(it->file));
                    x = next;
                }
            }
            auto label = misc ? std::string_view("misc.") : band.label;
            if (hi.x - lo.x >= LabelWidth(label)) {
                ImVec2 pos{lo.x + 4, lo.y + 3};
                draw->AddText(pos, textColor, label.data(), label.data() + label.size());
            }
            bool hit = hovered && ImGui::IsMouseHoveringRect(lo, hi);
            if (hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Select(range);
            if (hit) {
                draw->AddRect(lo, hi, textColor);
                if (selection == range) {
                    selectionMin = lo;
                    selectionMax = hi;
                    selectedHovered = true;
                }
            }
        }
        draw->PopClipRect();
        if (selectedHovered) DrawLogs(io.MouseWheel);
        return selectedHovered;
    }

    void DrawGraph() {
        bool selectedHovered = false;
        for (size_t i = 0; i < bands->threads.size(); ++i) {
            const auto& thread = bands->threads[i];
            std::string label = thread.name + " | Thread #" + std::to_string(thread.id) +
                " | " + std::to_string(thread.calls) + " logs###BenchmarkThread" + std::to_string(thread.id);
            bool wasExpanded = !collapsedThreads.contains(thread.id);
            ImGui::SetNextItemOpen(wasExpanded, ImGuiCond_Always);
            bool expanded = ImGui::CollapsingHeader(label.c_str());
            ImGui::SetItemTooltip("Expand this thread's rows, or collapse to a summary band. Summary bands retain nested call logs.");
            if (expanded != wasExpanded) {
                if (expanded) collapsedThreads.erase(thread.id);
                else collapsedThreads.insert(thread.id);
                selection.reset();
            }
            if (bands->extent <= 0) {
                ImGui::TextDisabled("No timed calls.");
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
            size_t first = expanded ? thread.firstRow : bands->rows.size() + i;
            size_t count = expanded ? thread.rowCount : 1;
            selectedHovered |= DrawThreadGraph(first, count);
            ImGui::PopID();
        }
        if (!selectedHovered) selection.reset();
    }

public:
    void Draw(bool& open) {
        if (!open) {
            if (result) {
                ResetModel();
                result.reset();
            }
            return;
        }
        ImGui::SetNextWindowSize({740, 320}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Benchmark", &open)) {
            DrawSettings();
            auto latest = Benchmark::Result();
            if (result != latest) {
                ResetModel();
                result = std::move(latest);
            }
            if (result && !result->frames.empty()) {
                DrawModes();
                if (!bands) bands = std::make_unique<BenchmarkBands>(*result, view, Group());
                DrawGraph();
                ImGui::TextDisabled("%zu frame(s)   #%llu - #%llu   |   %s",
                    result->frames.size(), static_cast<unsigned long long>(result->frames.front().number),
                    static_cast<unsigned long long>(result->frames.back().number),
                    view == BenchmarkView::Time ? "Sampled frame time" : "Share of measured self time");
                ImGui::TextWrapped("Wheel or middle-drag to zoom. Right-drag to pan. Click a band to inspect calls.");
            } else {
                ImGui::TextWrapped("Scene Play starts frame 1. Schedule a future scene frame, or use Benchmark Play while the scene is running.");
            }
        } else selection.reset();
        ImGui::End();
    }
};

} // namespace Rice
