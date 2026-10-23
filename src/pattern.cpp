#include "pattern.h"

#include <vector>

namespace {

struct Byte {
    uint8_t value;
    bool wildcard;
};

std::vector<Byte> Parse(const char* pattern)
{
    std::vector<Byte> out;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') {
            out.push_back({0, true});
            while (*p == '?') ++p;
            continue;
        }
        char hex[3] = {p[0], p[1], 0};
        out.push_back({static_cast<uint8_t>(strtoul(hex, nullptr, 16)), false});
        p += 2;
    }
    return out;
}

} // namespace

uint8_t* FindUniquePattern(HMODULE module, const char* pattern, int* matches)
{
    const std::vector<Byte> pat = Parse(pattern);
    auto* base = reinterpret_cast<uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

    uint8_t* found = nullptr;
    int count = 0;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* start = base + sec->VirtualAddress;
        size_t size = sec->Misc.VirtualSize;
        if (size < pat.size()) continue;
        for (size_t i = 0; i <= size - pat.size(); ++i) {
            size_t k = 0;
            while (k < pat.size() && (pat[k].wildcard || start[i + k] == pat[k].value)) ++k;
            if (k == pat.size()) {
                if (++count == 1) found = start + i;
            }
        }
    }
    if (matches) *matches = count;
    return count == 1 ? found : nullptr;
}
