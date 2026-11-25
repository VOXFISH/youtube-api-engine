#include "UiFont.h"
#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace Rice {
namespace {
bool LoadMapleGlyph(ImFontAtlas* atlas, ImFontConfig* source, ImFontBaked* baked, void* loaderData,
                    ImWchar codepoint, ImFontGlyph* glyph, float* advance) {
    // The TTF maps even undrawn Hangul syllables to a glyph with a nonzero advance.
    // Let the next font supply those syllables, for both measurement and drawing.
    const bool hangul = codepoint >= 0xAC00 && codepoint <= 0xD7A3;
    if (hangul && advance) {
        *advance = baked->FindGlyph(codepoint)->AdvanceX;
        return true;
    }
    const auto* stb = ImFontAtlasGetFontLoaderForStbTruetype();
    return stb->FontBakedLoadGlyph(atlas, source, baked, loaderData, codepoint, glyph, advance)
        && (!hangul || glyph->Visible);
}
}

void ApplyMapleStoryFont(float sizePixels) {
    auto resource = FindResourceW(nullptr, L"RICE_UI_FONT", MAKEINTRESOURCEW(10)); // RT_RCDATA
    auto handle = resource ? LoadResource(nullptr, resource) : nullptr;
    auto* bytes = handle ? LockResource(handle) : nullptr;
    auto size = resource ? SizeofResource(nullptr, resource) : 0;
    if (!bytes || !size) throw std::runtime_error("Missing embedded Maplestory Light font");

    static const ImFontLoader loader = [] {
        auto result = *ImFontAtlasGetFontLoaderForStbTruetype();
        result.FontBakedLoadGlyph = LoadMapleGlyph;
        return result;
    }();
    ImFontConfig config;
    config.FontLoader = &loader;
    config.FontDataOwnedByAtlas = false; // Windows owns the mapped resource for the process lifetime.
    std::snprintf(config.Name, sizeof(config.Name), "Maplestory Light");
    auto& io = ImGui::GetIO();
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF(bytes, static_cast<int>(size), sizePixels, &config);
    if (!io.FontDefault) throw std::runtime_error("Cannot load embedded Maplestory Light font");

    wchar_t windowsDir[MAX_PATH];
    const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (length && length < MAX_PATH) {
        const auto path = (std::filesystem::path(windowsDir) / "Fonts" / "malgun.ttf").u8string();
        ImFontConfig fallback;
        fallback.MergeMode = true;
        fallback.Flags |= ImFontFlags_NoLoadError;
        io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), sizePixels, &fallback);
    }
}
}
