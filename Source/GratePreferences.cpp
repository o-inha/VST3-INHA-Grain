#include "GratePreferences.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iterator>

namespace GratePreferences
{
namespace
{
constexpr wchar_t productKey[] = L"Software\\INHA\\Grate";

bool readBool (const wchar_t* name, bool fallback)
{
    DWORD value = 0, bytes = sizeof (value);
    if (RegGetValueW (HKEY_CURRENT_USER, productKey, name,
                      RRF_RT_REG_DWORD | RRF_ZEROONFAILURE, nullptr,
                      &value, &bytes) != ERROR_SUCCESS
        || bytes != sizeof (value) || value > 1)
        return fallback;
    return value == 1;
}

bool writeBool (const wchar_t* name, bool enabled, std::wstring& error)
{
    HKEY key = nullptr;
    auto result = RegCreateKeyExW (HKEY_CURRENT_USER, productKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS)
    {
        const DWORD value = enabled ? 1 : 0;
        // Write only this value, so another instance changing the other setting
        // cannot have its selection overwritten by an older in-memory snapshot.
        result = RegSetValueExW (key, name, 0, REG_DWORD,
            reinterpret_cast<const BYTE*> (&value), sizeof (value));
        RegCloseKey (key);
    }
    error.clear();
    if (result == ERROR_SUCCESS) return true;
    wchar_t message[512] {};
    FormatMessageW (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD> (result), 0, message,
        static_cast<DWORD> (std::size (message)), nullptr);
    error = L"The Grate preferences could not be saved for this Windows user.";
    if (message[0] != L'\0') error += std::wstring (L"\n\n") + message;
    return false;
}
}
Snapshot read()
{
    return { readBool (L"ThemeDark", false), readBool (L"Zoom150", true) };
}
bool setDarkTheme (bool enabled, std::wstring& error)
{
    return writeBool (L"ThemeDark", enabled, error);
}
bool setZoomed (bool enabled, std::wstring& error)
{
    return writeBool (L"Zoom150", enabled, error);
}
}
