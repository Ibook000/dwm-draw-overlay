#include "DwmFontLibrary.h"
#include "imgui.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>

namespace {
// Common families worth suggesting to callers; names are the canonical Windows ones.
const char* const kKnownFamilies[] = {
    "Microsoft YaHei", "SimHei", "SimSun", "DengXian", "KaiTi", "FangSong",
    "Consolas", "Arial", "Segoe UI", "Tahoma", "Verdana", "Times New Roman",
    "Courier New", "Cascadia Mono", "Noto Sans SC", "Source Han Sans SC"
};

// Last-resort faces used when a requested family cannot be resolved, so a typo still
// renders text instead of silently drawing nothing.
const char* const kCjkFallbacks[] = { "Microsoft YaHei", "SimHei", "SimSun", "DengXian", "Arial Unicode MS" };
const char* const kLatinFallbacks[] = { "Consolas", "Segoe UI", "Arial", "Tahoma" };

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
    return wide;
}

std::string Narrow(const wchar_t* wide) {
    if (!wide || !*wide) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string utf8(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &utf8[0], length, nullptr, nullptr);
    return utf8;
}

// Turns a registry value name such as "Microsoft YaHei & ... (TrueType)" into a bare
// family candidate list is unnecessary: we only strip the trailing "(TrueType)" marker
// and compare the whole remaining string case-insensitively.
std::string StripTypeMarker(const std::string& valueName) {
    std::string candidate = valueName;
    const size_t marker = candidate.rfind(" (");
    if (marker != std::string::npos && !candidate.empty() && candidate.back() == ')')
        candidate.erase(marker);
    return candidate;
}

// Expands a registry font value into an absolute path. Values are usually a bare file
// name relative to %WINDIR%\Fonts, but per-user installs store a full path.
std::string AbsoluteFontPath(const std::string& value) {
    if (value.empty()) return std::string();
    if (value.find(':') != std::string::npos) return FileExists(Widen(value)) ? value : std::string();
    if (value.find('\\') != std::string::npos) return FileExists(Widen(value)) ? value : std::string();

    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (length && length < MAX_PATH) {
        const std::string path = Narrow(windows) + "\\Fonts\\" + value;
        if (FileExists(Widen(path))) return path;
    }
    wchar_t local[MAX_PATH]{};
    const DWORD localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (localLength && localLength < MAX_PATH) {
        const std::string path = Narrow(local) + "\\Microsoft\\Windows\\Fonts\\" + value;
        if (FileExists(Widen(path))) return path;
    }
    return std::string();
}

// Searches "Microsoft\Windows NT\CurrentVersion\Fonts" for an exact family match.
// A value may map several families ("A & B (TrueType)"), so each '&'-separated part is
// compared individually; this is what makes "Microsoft YaHei" resolve to msyh.ttc.
std::string LookupFamilyInRegistry(HKEY root, const std::string& family) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
            0, KEY_READ, &key) != ERROR_SUCCESS) return std::string();

    const std::string wanted = DwmFontLibrary::toLower(family);
    std::string found;
    for (DWORD index = 0; found.empty(); ++index) {
        wchar_t name[512]{};
        BYTE data[1024]{};
        DWORD nameLength = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        DWORD dataLength = sizeof(data);
        DWORD type = 0;
        const LONG status = RegEnumValueW(key, index, name, &nameLength, nullptr, &type, data, &dataLength);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) break;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

        const std::string candidate = StripTypeMarker(Narrow(name));
        // Split on '&' so "A & B (TrueType)" matches either name.
        size_t start = 0;
        while (start <= candidate.size()) {
            const size_t ampersand = candidate.find('&', start);
            std::string part = candidate.substr(start, ampersand == std::string::npos
                ? std::string::npos : ampersand - start);
            // Trim surrounding whitespace.
            const size_t first = part.find_first_not_of(" \t");
            const size_t last = part.find_last_not_of(" \t");
            part = first == std::string::npos ? std::string() : part.substr(first, last - first + 1);
            if (DwmFontLibrary::toLower(part) == wanted) {
                found = AbsoluteFontPath(Narrow(reinterpret_cast<const wchar_t*>(data)));
                break;
            }
            if (ampersand == std::string::npos) break;
            start = ampersand + 1;
        }
    }
    RegCloseKey(key);
    return found;
}

bool LooksLikeCjk(const std::string& lower) {
    static const char* const markers[] = {
        "yahei", "simhei", "simsun", "dengxian", "kaiti", "fangsong", "nsimsun",
        "noto sans sc", "noto sans cjk", "source han", "msyh", "hei", "song", "kai", "ming", "gothic"
    };
    for (const char* marker : markers)
        if (lower.find(marker) != std::string::npos) return true;
    return false;
}
}

std::string DwmFontLibrary::toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return value;
}

const char* const* DwmFontLibrary::knownFamilies(size_t& count) {
    count = sizeof(kKnownFamilies) / sizeof(kKnownFamilies[0]);
    return kKnownFamilies;
}

std::string DwmFontLibrary::resolveFile(const std::string& family) {
    const auto cached = m_files.find(family);
    if (cached != m_files.end()) return cached->second;

    std::string resolved;
    const std::wstring wide = Widen(family);
    // A path (drive letter or directory separator) is used verbatim.
    if (family.find(':') != std::string::npos || family.find('\\') != std::string::npos ||
        family.find('/') != std::string::npos) {
        if (FileExists(wide)) resolved = family;
    } else if (!family.empty()) {
        resolved = LookupFamilyInRegistry(HKEY_LOCAL_MACHINE, family);
        if (resolved.empty()) resolved = LookupFamilyInRegistry(HKEY_CURRENT_USER, family);
    }
    m_files[family] = resolved;
    return resolved;
}

const ImWchar* DwmFontLibrary::rangesFor(int ranges, const std::string& hint) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    switch (ranges) {
    case DwmRanges_Default:  return atlas->GetGlyphRangesDefault();
    case DwmRanges_Japanese: return atlas->GetGlyphRangesJapanese();
    case DwmRanges_Korean:   return atlas->GetGlyphRangesKorean();
    case DwmRanges_Cyrillic: return atlas->GetGlyphRangesCyrillic();
    case DwmRanges_Chinese:  return atlas->GetGlyphRangesChineseSimplifiedCommon();
    default:
        // Auto keeps the historical behaviour (common simplified Chinese) for CJK-capable
        // faces, and stays light on Latin-only ones.
        return LooksLikeCjk(toLower(hint))
            ? atlas->GetGlyphRangesChineseSimplifiedCommon()
            : atlas->GetGlyphRangesDefault();
    }
}

ImFont* DwmFontLibrary::acquire(const DwmFontSpec& spec, float& outSize) {
    const std::string family(spec.m_Family);
    // Resolve range intent once, so the cache key reflects what will actually be loaded.
    std::string hint = family;
    std::string file = family.empty() ? std::string() : resolveFile(family);
    if (file.empty()) {
        const bool cjk = family.empty() || LooksLikeCjk(toLower(family));
        const char* const* candidates = cjk ? kCjkFallbacks : kLatinFallbacks;
        const size_t count = cjk ? (sizeof(kCjkFallbacks) / sizeof(kCjkFallbacks[0]))
                                 : (sizeof(kLatinFallbacks) / sizeof(kLatinFallbacks[0]));
        for (size_t i = 0; i < count && file.empty(); ++i) file = resolveFile(candidates[i]);
        if (!family.empty()) {
            m_warnings.push_back("字体未找到，已回退：" + family);
            hint = file;
        }
    }

    const float size = spec.m_Size > 0 ? spec.m_Size : DEFAULT_FONT_SIZE;
    outSize = size;

    char key[FONT_NAME_LEN + 48];
    sprintf_s(key, "%s\x1f%.2f\x1f%d", file.c_str(), size, spec.m_Ranges);
    const auto cached = m_fonts.find(key);
    if (cached != m_fonts.end()) {
        outSize = cached->second.size;
        return cached->second.font;
    }

    ImFont* font = nullptr;
    if (!file.empty()) {
        const int effectiveRanges = spec.m_Ranges == DwmRanges_Auto && LooksLikeCjk(toLower(hint))
            ? DwmRanges_Chinese : spec.m_Ranges;
        // Existence was verified in resolveFile, so AddFontFromFileTTF will not assert.
        font = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.c_str(), size, nullptr,
            rangesFor(effectiveRanges, hint));
    }
    if (font) m_atlasDirty = true;
    m_fonts[key] = Entry{ font, size };
    return font;
}

bool DwmFontLibrary::consumeAtlasDirty() {
    const bool dirty = m_atlasDirty;
    m_atlasDirty = false;
    return dirty;
}

std::vector<std::string> DwmFontLibrary::takeWarnings() {
    std::vector<std::string> result;
    result.swap(m_warnings);
    return result;
}
