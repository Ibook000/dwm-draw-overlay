// Unit test for DwmFontLibrary family resolution and atlas interning.
// Validates that real Windows families resolve to actual font files rather than silently
// falling back, that identical requests are interned, and that size is honoured.
#include "DwmFontLibrary.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#define CHECK(expr) do { if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main() {
    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    CHECK(context != nullptr);
    ImGui::SetCurrentContext(context);
    // Fonts can also be resolved for a Chinese-capable default.
    ImGui::GetIO().Fonts->AddFontDefault();

    DwmFontLibrary library;

    float size = 0;
    // A real Windows family must resolve; the returned font comes from the atlas.
    const DwmFontSpec consolas{"Consolas", 14, DwmRanges_Default};
    ImFont* mono = library.acquire(consolas, size);
    CHECK(mono != nullptr);
    CHECK(size == 14.0f);
    // The atlas must be flagged dirty so the caller re-uploads the texture.
    CHECK(library.consumeAtlasDirty());
    CHECK(!library.consumeAtlasDirty()); // consumed exactly once
    // Interning: the very same request returns the identical ImFont without new atlas work.
    float again = 0;
    ImFont* mono2 = library.acquire(consolas, again);
    CHECK(mono2 == mono);
    CHECK(!library.consumeAtlasDirty());

    // A CJK family resolves too, with the Chinese range set.
    ImFont* cjk = library.acquire(DwmFontSpec{"Microsoft YaHei", 20, DwmRanges_Auto}, size);
    CHECK(cjk != nullptr);
    CHECK(cjk != mono);
    CHECK(size == 20.0f);

    // A different size produces a distinct font at the same family.
    ImFont* bigger = library.acquire(DwmFontSpec{"Consolas", 28, DwmRanges_Default}, size);
    CHECK(bigger != nullptr);
    CHECK(bigger != mono);

    // An unresolvable family falls back to a real face and reports a warning once.
    ImFont* fallback = library.acquire(DwmFontSpec{"No Such Family 12345", 16, DwmRanges_Auto}, size);
    CHECK(fallback != nullptr);
    const auto warnings = library.takeWarnings();
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].find("No Such Family 12345") != std::string::npos);
    CHECK(library.takeWarnings().empty()); // consumed

    // A full path to a real file is used verbatim.
    const char* known = "C:\\Windows\\Fonts\\consola.ttf";
    if (GetFileAttributesA(known) != INVALID_FILE_ATTRIBUTES) {
        DwmFontSpec directSpec{};
        std::strncpy(directSpec.m_Family, known, FONT_NAME_LEN - 1);
        directSpec.m_Size = 13;
        directSpec.m_Ranges = DwmRanges_Default;
        ImFont* direct = library.acquire(directSpec, size);
        CHECK(direct != nullptr);
        CHECK(direct != mono); // different size, so a different entry
    }

    // Size <= 0 falls back to the shared default size.
    ImFont* defaultSized = library.acquire(DwmFontSpec{"Consolas", 0, DwmRanges_Default}, size);
    CHECK(defaultSized != nullptr);
    CHECK(size == DEFAULT_FONT_SIZE);

    // Known family suggestions are exposed for callers.
    size_t count = 0;
    const char* const* families = DwmFontLibrary::knownFamilies(count);
    CHECK(families != nullptr && count > 0);
    CHECK(std::strlen(families[0]) > 0);

    ImGui::DestroyContext(context);
    puts("PASS: font family resolution, path loading, interning, sizes and fallback warnings.");
    return 0;
}
