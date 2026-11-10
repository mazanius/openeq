#include "Log.h"

#include <OpenEqShared.h>

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace openeq::apo {

namespace {
std::mutex g_logMutex;
constexpr LONGLONG kMaxLogBytes = 1024 * 1024;
} // namespace

std::wstring dataDir()
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path))) {
        result = path;
        result += L"\\";
        result += shared::kDataDirName;
    }
    CoTaskMemFree(path);
    return result;
}

void log(const wchar_t* fmt, ...)
{
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu:%lu] %s\r\n", st.wYear, st.wMonth,
                 st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId(),
                 GetCurrentThreadId(), msg);

    OutputDebugStringW(line);

    const std::wstring dir = dataDir();
    if (dir.empty()) return;
    const std::wstring file = dir + L"\\" + shared::kApoLogName;

    std::lock_guard lock(g_logMutex);
    HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA | FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > kMaxLogBytes) {
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
    }
    SetFilePointer(h, 0, nullptr, FILE_END);

    char utf8[2400];
    const int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof utf8, nullptr, nullptr);
    if (n > 1) {
        DWORD written = 0;
        WriteFile(h, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
    }
    CloseHandle(h);
}

} // namespace openeq::apo
