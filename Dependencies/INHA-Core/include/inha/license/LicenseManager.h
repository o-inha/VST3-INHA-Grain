#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

namespace InhaLicense
{
inline constexpr wchar_t helpUrl[] = L"https://inha.jp/help";

struct Product
{
    std::string id;
    std::wstring displayName;
    std::string version;
    std::string fingerprintNamespace;
};

enum class Status : int
{
    checking,
    allowed,
    invalidLocalKey,
    denied
};

std::wstring cleanMessage (const std::wstring&);
std::wstring messageWithHelp (const std::wstring&);

class Manager
{
public:
    explicit Manager (Product);
    ~Manager();

    Manager (const Manager&) = delete;
    Manager& operator= (const Manager&) = delete;

    void start (HINSTANCE);
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool denied() const noexcept;
    [[nodiscard]] std::wstring message() const;

private:
    static DWORD WINAPI threadEntry (LPVOID);
    void validate();
    void setDecision (Status, std::wstring message = {});

    Product product;
    HINSTANCE module = nullptr;
    std::atomic<Status> currentStatus { Status::checking };
    mutable std::mutex messageMutex;
    std::wstring currentMessage;
    HANDLE worker = nullptr;
};
}
