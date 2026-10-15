#pragma once
#include "DrawProtocol.h"
#include <string>
#include <unordered_map>
#include <vector>

// Resolves font requests to real font files and interns them in the ImGui atlas.
//
// Design notes:
//  * Nothing here runs inside ImGui::NewFrame()..ImGui::Render(); the atlas is Locked
//    during that window and adding fonts would trip an ImGui assertion.
//  * Identical (family, size, ranges) requests are created once and reused, so a producer
//    that names its font on every frame does not rebuild the atlas every frame.
//  * ImFont objects are heap allocated and stay valid when ImFontAtlas::Fonts grows,
//    so cached ImFont* pointers survive later additions.
class DwmFontLibrary {
public:
    // Resolves spec and returns the font to draw with.
    // A spec with m_Size <= 0 uses DEFAULT_FONT_SIZE.
    // Returns nullptr when no real font could be produced; the caller then falls back to
    // the ImGui default font but still honours the requested size.
    // outSize receives the pixel size to pass to ImDrawList::AddText.
    ImFont* acquire(const DwmFontSpec& spec, float& outSize);

    // True when acquire() added a font since the last call: the atlas texture must be
    // re-uploaded before the next frame is rendered.
    bool consumeAtlasDirty();

    // Families that failed to resolve and fell back, for surfacing once.
    std::vector<std::string> takeWarnings();

    // Suggested family names, for documentation and introspection.
    static const char* const* knownFamilies(size_t& count);

    // Lowercases ASCII, used for family matching. Public so the registry lookup shares it.
    static std::string toLower(std::string value);

private:
    struct Entry { ImFont* font = nullptr; float size = 0; };
    std::string resolveFile(const std::string& family);
    const ImWchar* rangesFor(int ranges, const std::string& family);

    bool m_atlasDirty = false;
    std::unordered_map<std::string, Entry> m_fonts;       // cache key -> font
    std::unordered_map<std::string, std::string> m_files; // family -> resolved path ("" = failed)
    std::vector<std::string> m_warnings;
};
