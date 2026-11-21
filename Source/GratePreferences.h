#pragma once
#include <string>

namespace GratePreferences
{
struct Snapshot
{
    bool darkTheme = false;
    bool zoomed = true;
};
// Per Windows user, across Grate instances and hosts. Never read shared INHA
// appearance values or serialize these choices into projects/presets.
Snapshot read();
bool setDarkTheme (bool enabled, std::wstring& error);
bool setZoomed (bool enabled, std::wstring& error);
}
