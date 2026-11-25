#include "Platform/Windows/UiFont.h"
#include <imgui.h>
#include <iostream>

// Some syllables have a character mapping but no outline in Maplestory Light.
// Check layout and rendering agree before the first glyph is cached.
bool CheckFont(float size) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    Rice::ApplyMapleStoryFont(size);
    ImGui::NewFrame();
    auto* font = ImGui::GetFont();
    auto* baked = ImGui::GetFontBaked();
    baked->LoadNoRenderOnLayout = true;
    bool ok = font == io.FontDefault && ImGui::GetFontSize() == size;
    for (const ImWchar ch : {u'R', u'한', u'힣'}) {
        const float advance = baked->GetCharAdvance(ch);
        const auto* glyph = baked->FindGlyph(ch);
        ImWchar expectedChar = ch;
        unsigned expectedSource = 0;
        if (ch == u'힣') {
            if (font->Sources.Size > 1) expectedSource = 1;
            else expectedChar = font->FallbackChar;
        }
        ok = ok && glyph->Codepoint == expectedChar && glyph->SourceIdx == expectedSource
            && glyph->Visible && advance > 0 && advance == glyph->AdvanceX;
    }
    ImGui::EndFrame();
    ImGui::DestroyContext();
    return ok;
}

int main() {
    if (CheckFont(13.0f) && CheckFont(17.0f)) return 0;
    std::cerr << "Unexpected UI font, glyph source or advance\n";
    return 1;
}
