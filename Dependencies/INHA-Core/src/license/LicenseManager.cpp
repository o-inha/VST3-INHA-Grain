#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include "inha/license/LicenseManager.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace InhaLicense
{
namespace
{
std::string wideToUtf8 (const std::wstring& value)
{
    if (value.empty())
        return {};
    const auto size = WideCharToMultiByte (CP_UTF8, 0, value.c_str(),
                                           static_cast<int> (value.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string result (static_cast<std::size_t> (std::max (0, size)), '\0');
    if (size > 0)
        WideCharToMultiByte (CP_UTF8, 0, value.c_str(), static_cast<int> (value.size()),
                             result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide (const std::string& value)
{
    if (value.empty())
        return {};
    const auto size = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int> (value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result (static_cast<std::size_t> (size), L'\0');
    MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                         static_cast<int> (value.size()), result.data(), size);
    return result;
}

bool isHexLicenseKey (const std::string& value) noexcept
{
    if (value.size() != 64)
        return false;
    return std::all_of (value.begin(), value.end(), [] (char character)
    {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f');
    });
}

std::string jsonStringField (const std::string& json, const char* field)
{
    const auto marker = std::string ("\"") + field + "\"";
    auto position = json.find (marker);
    if (position == std::string::npos)
        return {};
    position = json.find (':', position + marker.size());
    if (position == std::string::npos)
        return {};
    position = json.find_first_not_of (" \t\r\n", position + 1);
    if (position == std::string::npos || json[position] != '"')
        return {};
    ++position;

    std::string result;
    bool escaped = false;
    for (; position < json.size(); ++position)
    {
        const auto character = json[position];
        if (! escaped && character == '"')
            return result;
        if (! escaped && character == '\\')
        {
            escaped = true;
            continue;
        }
        if (escaped)
        {
            switch (character)
            {
                case 'n': result.push_back ('\n'); break;
                case 'r': result.push_back ('\r'); break;
                case 't': result.push_back ('\t'); break;
                case '"': result.push_back ('"'); break;
                case '\\': result.push_back ('\\'); break;
                case '/': result.push_back ('/'); break;
                case 'u':
                {
                    const auto hexValue = [] (char digit) -> int
                    {
                        if (digit >= '0' && digit <= '9') return digit - '0';
                        if (digit >= 'a' && digit <= 'f') return 10 + digit - 'a';
                        if (digit >= 'A' && digit <= 'F') return 10 + digit - 'A';
                        return -1;
                    };
                    if (position + 4 >= json.size())
                        return {};
                    std::uint32_t codePoint = 0;
                    for (int index = 1; index <= 4; ++index)
                    {
                        const auto digit = hexValue (
                            json[position + static_cast<std::size_t> (index)]);
                        if (digit < 0)
                            return {};
                        codePoint = (codePoint << 4u) | static_cast<std::uint32_t> (digit);
                    }
                    position += 4;
                    if (codePoint <= 0x7fu)
                        result.push_back (static_cast<char> (codePoint));
                    else if (codePoint <= 0x7ffu)
                    {
                        result.push_back (static_cast<char> (0xc0u | (codePoint >> 6u)));
                        result.push_back (static_cast<char> (0x80u | (codePoint & 0x3fu)));
                    }
                    else
                    {
                        result.push_back (static_cast<char> (0xe0u | (codePoint >> 12u)));
                        result.push_back (static_cast<char> (
                            0x80u | ((codePoint >> 6u) & 0x3fu)));
                        result.push_back (static_cast<char> (0x80u | (codePoint & 0x3fu)));
                    }
                    break;
                }
                default: result.push_back (character); break;
            }
            escaped = false;
        }
        else
        {
            result.push_back (character);
        }
    }
    return {};
}

std::string sha256Hex (const std::string& input)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    std::vector<UCHAR> object;
    std::array<UCHAR, 32> digest {};

    if (BCryptOpenAlgorithmProvider (&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0
        || BCryptGetProperty (algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR> (&objectSize), sizeof (objectSize),
                              &resultSize, 0) < 0)
        goto cleanup;
    object.resize (objectSize);
    if (BCryptCreateHash (algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0
        || BCryptHashData (hash, reinterpret_cast<PUCHAR> (
                               const_cast<char*> (input.data())),
                           static_cast<ULONG> (input.size()), 0) < 0
        || BCryptFinishHash (hash, digest.data(), static_cast<ULONG> (digest.size()), 0) < 0)
        digest.fill (0);

cleanup:
    if (hash != nullptr)
        BCryptDestroyHash (hash);
    if (algorithm != nullptr)
        BCryptCloseAlgorithmProvider (algorithm, 0);

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve (64);
    for (const auto byte : digest)
    {
        result.push_back (hex[byte >> 4]);
        result.push_back (hex[byte & 15]);
    }
    return result;
}

std::string deviceFingerprint (const std::string& fingerprintNamespace)
{
    wchar_t machineGuid[256] {};
    DWORD bytes = sizeof (machineGuid);
    const auto status = RegGetValueW (HKEY_LOCAL_MACHINE,
                                      L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                                      RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr,
                                      machineGuid, &bytes);
    std::wstring source = status == ERROR_SUCCESS ? machineGuid : L"";
    if (source.empty())
    {
        wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] {};
        DWORD length = static_cast<DWORD> (std::size (computerName));
        if (GetComputerNameW (computerName, &length))
            source.assign (computerName, length);
    }
    return sha256Hex (fingerprintNamespace + wideToUtf8 (source));
}

std::string readTextFile (const std::wstring& path)
{
    const auto file = CreateFileW (path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    LARGE_INTEGER size {};
    std::string result;
    if (GetFileSizeEx (file, &size) && size.QuadPart > 0 && size.QuadPart <= 4096)
    {
        result.resize (static_cast<std::size_t> (size.QuadPart));
        DWORD read = 0;
        if (! ReadFile (file, result.data(), static_cast<DWORD> (result.size()), &read, nullptr))
            result.clear();
        else
            result.resize (read);
    }
    CloseHandle (file);
    while (! result.empty()
           && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
        result.pop_back();
    return result;
}

std::wstring bundleResourcePath (HINSTANCE module, const wchar_t* filename)
{
    wchar_t modulePath[MAX_PATH] {};
    if (module == nullptr || GetModuleFileNameW (module, modulePath, MAX_PATH) == 0)
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

std::string jsonEscape (const std::string& value)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve (value.size() + 16);
    for (const auto byte : value)
    {
        const auto character = static_cast<unsigned char> (byte);
        switch (character)
        {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20u)
                {
                    result += "\\u00";
                    result.push_back (hex[character >> 4u]);
                    result.push_back (hex[character & 0x0fu]);
                }
                else
                {
                    result.push_back (static_cast<char> (character));
                }
                break;
        }
    }
    return result;
}

bool ensureDirectory (const std::wstring& path)
{
    if (CreateDirectoryW (path.c_str(), nullptr) != FALSE)
        return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    const auto attributes = GetFileAttributesW (path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring localLicenseStatePath (const std::string& key, const std::string& productId)
{
    std::array<wchar_t, 32768> localAppData {};
    const auto length = GetEnvironmentVariableW (L"LOCALAPPDATA", localAppData.data(),
                                                  static_cast<DWORD> (localAppData.size()));
    if (length == 0 || length >= static_cast<DWORD> (localAppData.size()))
        return {};

    auto directory = std::wstring (localAppData.data(), length) + L"\\INHA";
    if (! ensureDirectory (directory))
        return {};
    directory += L"\\Licences";
    if (! ensureDirectory (directory))
        return {};

    return directory + L"\\" + utf8ToWide (productId) + L"-"
         + utf8ToWide (sha256Hex (key)) + L".json";
}

bool writeTextFileAtomic (const std::wstring& path, const std::string& content)
{
    if (content.size() > MAXDWORD)
        return false;
    const auto contentSize = static_cast<DWORD> (content.size());
    static std::atomic<unsigned long> temporaryCounter { 0 };
    const auto temporaryPath = path + L".tmp-" + std::to_wstring (GetCurrentProcessId())
                             + L"-" + std::to_wstring (GetCurrentThreadId())
                             + L"-" + std::to_wstring (
                                   temporaryCounter.fetch_add (1, std::memory_order_relaxed));
    const auto file = CreateFileW (temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const auto writeSucceeded = content.empty()
        || (WriteFile (file, content.data(), contentSize, &written, nullptr) != FALSE
            && written == contentSize);
    const auto flushSucceeded = writeSucceeded && FlushFileBuffers (file) != FALSE;
    CloseHandle (file);
    if (! flushSucceeded
        || MoveFileExW (temporaryPath.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        DeleteFileW (temporaryPath.c_str());
        return false;
    }
    return true;
}

bool isExplicitDenialStatus (const std::string& status, const std::string& reason)
{
    return status == "denied"
        || status == "device_limit"
        || status == "max_activations_reached"
        || status == "license_inactive"
        || status == "disabled"
        || status == "invalid"
        || status == "refunded"
        || status == "revoked"
        || status == "suspended"
        || reason == "max_activations"
        || reason == "license_inactive";
}

std::wstring denialMessage (const std::string& reason, const std::string& customMessage)
{
    if (reason == "max_activations")
        return messageWithHelp (L"Too many device activations");
    const auto custom = utf8ToWide (customMessage);
    return messageWithHelp (custom.empty()
                                ? std::wstring (L"This plugin license is not active.")
                                : custom);
}

void writeLicenseState (const std::wstring& path, const std::string& productId,
                        const std::string& status, const std::string& reason,
                        const std::string& licenseStatus, const std::wstring& message)
{
    if (path.empty())
        return;
    const auto json = std::string ("{\"product\":\"") + productId
                    + "\",\"status\":\"" + jsonEscape (status)
                    + "\",\"reason\":\"" + jsonEscape (reason)
                    + "\",\"license_status\":\"" + jsonEscape (licenseStatus)
                    + "\",\"status_message\":\"" + jsonEscape (wideToUtf8 (message))
                    + "\"}\n";
    writeTextFileAtomic (path, json);
}
}

std::wstring cleanMessage (const std::wstring& message)
{
    std::wstring result;
    std::size_t start = 0;
    while (start <= message.size())
    {
        const auto end = message.find (L'\n', start);
        auto line = message.substr (start, end == std::wstring::npos
                                              ? std::wstring::npos : end - start);
        if (! line.empty() && line.back() == L'\r')
            line.pop_back();
        const auto first = line.find_first_not_of (L" \t");
        const auto last = line.find_last_not_of (L" \t");
        line = first == std::wstring::npos ? std::wstring {}
                                           : line.substr (first, last - first + 1);

        if (! line.empty()
            && line.find (L"@inha.jp") == std::wstring::npos
            && line != helpUrl)
        {
            if (! result.empty())
                result += L'\n';
            result += line;
        }
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return result;
}

std::wstring messageWithHelp (const std::wstring& message)
{
    auto cleaned = cleanMessage (message);
    if (cleaned.empty())
        cleaned = L"This plugin license is not active.";
    return cleaned + L"\n\n" + helpUrl;
}

Manager::Manager (Product newProduct)
    : product (std::move (newProduct))
{
}

Manager::~Manager()
{
    if (worker != nullptr)
    {
        WaitForSingleObject (worker, INFINITE);
        CloseHandle (worker);
    }
}

void Manager::start (HINSTANCE newModule)
{
    if (worker != nullptr)
        return;
    module = newModule;
    worker = CreateThread (nullptr, 0, &Manager::threadEntry, this, 0, nullptr);
}

Status Manager::status() const noexcept
{
    return currentStatus.load (std::memory_order_acquire);
}

bool Manager::denied() const noexcept
{
    const auto decision = status();
    return decision == Status::invalidLocalKey || decision == Status::denied;
}

std::wstring Manager::message() const
{
    const std::lock_guard<std::mutex> lock (messageMutex);
    return currentMessage;
}

DWORD WINAPI Manager::threadEntry (LPVOID context)
{
    static_cast<Manager*> (context)->validate();
    return 0;
}

void Manager::setDecision (Status decision, std::wstring message)
{
    if (decision == Status::denied)
        message = messageWithHelp (message);
    {
        const std::lock_guard<std::mutex> lock (messageMutex);
        currentMessage = std::move (message);
    }
    currentStatus.store (decision, std::memory_order_release);
}

void Manager::validate()
{
    auto key = readTextFile (bundleResourcePath (module, L"inha-licence.key"));
    if (key.empty())
        key = readTextFile (bundleResourcePath (module, L"inha-license.key"));
    if (! isHexLicenseKey (key))
    {
        currentStatus.store (Status::invalidLocalKey, std::memory_order_release);
        return;
    }

    const auto statePath = localLicenseStatePath (key, product.id);
    const auto savedState = statePath.empty() ? std::string {} : readTextFile (statePath);
    const auto savedStatus = jsonStringField (savedState, "status");
    const auto savedReason = jsonStringField (savedState, "reason");
    if (isExplicitDenialStatus (savedStatus, savedReason))
        setDecision (Status::denied,
                     denialMessage (savedReason,
                                    jsonStringField (savedState, "status_message")));

    const auto body = std::string ("{\"key\":\"") + key + "\",\"device\":\""
                    + deviceFingerprint (product.fingerprintNamespace)
                    + "\",\"product\":\"" + product.id
                    + "\",\"version\":\"" + product.version + "\"}";
    const auto userAgent = product.displayName + L"/" + utf8ToWide (product.version);
    const auto session = WinHttpOpen (userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return;

    WinHttpSetTimeouts (session, 3000, 3000, 3000, 5000);
    const auto connection = WinHttpConnect (session, L"inha.jp",
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
    const auto request = connection != nullptr
        ? WinHttpOpenRequest (connection, L"POST", L"/activate", nullptr, WINHTTP_NO_REFERER,
                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    std::string response;
    bool received = false;
    if (request != nullptr)
    {
        const wchar_t headers[] = L"Content-Type: application/json\r\n";
        if (WinHttpSendRequest (request, headers, static_cast<DWORD> (-1L),
                                const_cast<char*> (body.data()),
                                static_cast<DWORD> (body.size()),
                                static_cast<DWORD> (body.size()), 0)
            && WinHttpReceiveResponse (request, nullptr))
        {
            DWORD httpStatus = 0;
            DWORD statusBytes = sizeof (httpStatus);
            WinHttpQueryHeaders (request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &httpStatus, &statusBytes,
                                 WINHTTP_NO_HEADER_INDEX);
            for (;;)
            {
                DWORD available = 0;
                if (! WinHttpQueryDataAvailable (request, &available) || available == 0)
                    break;
                const auto oldSize = response.size();
                response.resize (oldSize + available);
                DWORD read = 0;
                if (! WinHttpReadData (request, response.data() + oldSize, available, &read))
                    break;
                response.resize (oldSize + read);
                if (response.size() > 8192)
                    break;
            }
            received = httpStatus >= 200 && httpStatus < 500;
        }
    }

    if (request != nullptr)
        WinHttpCloseHandle (request);
    if (connection != nullptr)
        WinHttpCloseHandle (connection);
    WinHttpCloseHandle (session);
    if (! received)
        return;

    const auto statusValue = jsonStringField (response, "status");
    const auto reason = jsonStringField (response, "reason");
    const auto masterStatus = jsonStringField (response, "license_status");
    if (statusValue == "ok" || statusValue == "active" || statusValue == "allowed")
    {
        writeLicenseState (statePath, product.id, "active", "", "active", std::wstring {});
        setDecision (Status::allowed);
    }
    else if (statusValue == "device_limit" || statusValue == "max_activations_reached"
             || (statusValue == "denied" && reason == "max_activations"))
    {
        auto denial = denialMessage ("max_activations",
                                     jsonStringField (response, "status_message"));
        writeLicenseState (statePath, product.id, "denied", "max_activations", masterStatus, denial);
        setDecision (Status::denied, std::move (denial));
    }
    else if (isExplicitDenialStatus (statusValue, reason))
    {
        auto denial = denialMessage (reason, jsonStringField (response, "status_message"));
        writeLicenseState (statePath, product.id, statusValue, reason,
                           masterStatus.empty() ? statusValue : masterStatus, denial);
        setDecision (Status::denied, std::move (denial));
    }
}
}


