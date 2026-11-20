#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace Inha::Platform
{
[[nodiscard]] inline std::wstring bundleResourcePath (HINSTANCE module,
                                                      const wchar_t* filename)
{
    wchar_t modulePath[MAX_PATH] {};
    if (module == nullptr || filename == nullptr
        || GetModuleFileNameW (module, modulePath, MAX_PATH) == 0)
        return {};

    std::wstring result (modulePath);
    for (int level = 0; level < 2; ++level)
    {
        const auto separator = result.find_last_of (L"\\/");
        if (separator == std::wstring::npos)
            return {};
        result.resize (separator);
    }
    result += L"\\Resources\\";
    result += filename;
    return result;
}
}

#endif
