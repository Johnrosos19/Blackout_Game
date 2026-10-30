#pragma once
// ---------------------------------------------------------------------------
// Paths.h - find an asset whether the .exe is run from Visual Studio (working
// dir = project folder) or double-clicked from x64/Debug (B14: no machine
// specific absolute paths anywhere).
// ---------------------------------------------------------------------------
#include <string>
#include <fstream>

inline std::string ResolveAsset(const std::string& rel)
{
    static const char* prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    for (const char* p : prefixes)
    {
        std::string candidate = std::string(p) + rel;
        std::ifstream f(candidate, std::ios::binary);
        if (f.good()) return candidate;
    }
    return rel;
}
