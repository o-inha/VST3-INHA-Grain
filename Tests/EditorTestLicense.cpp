// Linked exclusively into GrateEditorFixture, never the release VST3.
#ifndef INHA_EDITOR_TEST_FIXTURE
#error "The mock licence dependency must only be built for the editor test fixture."
#endif
#include <inha/license/LicenseManager.h>
#include <utility>

namespace InhaLicense
{
std::wstring cleanMessage (const std::wstring& value) { return value; }
std::wstring messageWithHelp (const std::wstring& value) { return value + L"\n\n" + helpUrl; }
Manager::Manager (Product value) : product (std::move (value)) {}
Manager::~Manager() = default;
void Manager::start (HINSTANCE value)
{
    module = value;
    currentStatus.store (Status::allowed, std::memory_order_release);
}
Status Manager::status() const noexcept { return currentStatus.load (std::memory_order_acquire); }
bool Manager::denied() const noexcept { return false; }
std::wstring Manager::message() const { return {}; }
}
