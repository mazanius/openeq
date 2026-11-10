#include "ConfigWatcher.h"

#include "Log.h"

#include <vector>

namespace openeq::apo {

namespace {

constexpr DWORD kIdleIntervalMs = 50;
constexpr DWORD kRetryIntervalMs = 1000;
constexpr DWORD kMaxConfigBytes = 64 * 1024;

bool readFile(const std::wstring& path, std::string& out)
{
    // The UI replaces the file atomically, but retry a few times in case we
    // race with an editor holding it open.
    for (int attempt = 0; attempt < 5; ++attempt) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return false;
            Sleep(10);
            continue;
        }
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(h, &size) && size.QuadPart <= kMaxConfigBytes;
        if (ok) {
            out.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            ok = size.QuadPart == 0 ||
                 (ReadFile(h, out.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
                  read == static_cast<DWORD>(size.QuadPart));
        }
        CloseHandle(h);
        return ok;
    }
    return false;
}

} // namespace

ConfigWatcher::~ConfigWatcher()
{
    stop();
}

void ConfigWatcher::start(std::wstring directory, std::wstring fileName, ConfigCallback onConfig, IdleCallback onIdle)
{
    stop();
    m_dir = std::move(directory);
    m_path = m_dir + L"\\" + fileName;
    m_onConfig = std::move(onConfig);
    m_onIdle = std::move(onIdle);
    m_lastText.clear();

    reload();

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        log(L"ConfigWatcher: CreateEvent failed (%lu)", GetLastError());
        return;
    }
    m_thread = std::thread([this] { run(); });
}

void ConfigWatcher::stop()
{
    if (m_thread.joinable()) {
        SetEvent(m_stopEvent);
        m_thread.join();
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

bool ConfigWatcher::reload()
{
    std::string text;
    if (!readFile(m_path, text)) return false;
    if (text == m_lastText) return false;
    m_lastText = text;

    auto cfg = parse(text);
    if (!cfg) {
        log(L"Config %s could not be parsed; keeping previous settings", m_path.c_str());
        return false;
    }
    m_onConfig(*cfg);
    return true;
}

void ConfigWatcher::run()
{
    SetThreadDescription(GetCurrentThread(), L"openeq config watcher");
    HANDLE change = INVALID_HANDLE_VALUE;

    for (;;) {
        if (change == INVALID_HANDLE_VALUE) {
            change = FindFirstChangeNotificationW(m_dir.c_str(), FALSE,
                                                  FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                                      FILE_NOTIFY_CHANGE_SIZE);
            if (change != INVALID_HANDLE_VALUE) reload(); // directory may have just appeared
        }

        if (change == INVALID_HANDLE_VALUE) {
            // Directory missing or inaccessible: keep idling and retry.
            for (DWORD waited = 0; waited < kRetryIntervalMs; waited += kIdleIntervalMs) {
                if (WaitForSingleObject(m_stopEvent, kIdleIntervalMs) == WAIT_OBJECT_0) return;
                m_onIdle();
            }
            continue;
        }

        HANDLE handles[2] = {m_stopEvent, change};
        const DWORD r = WaitForMultipleObjects(2, handles, FALSE, kIdleIntervalMs);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) {
            reload();
            if (!FindNextChangeNotification(change)) {
                FindCloseChangeNotification(change);
                change = INVALID_HANDLE_VALUE;
            }
        }
        m_onIdle();
    }

    if (change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change);
}

} // namespace openeq::apo
