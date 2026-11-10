#pragma once
// Background thread that watches the per-endpoint config file and reports
// changes. Also runs a periodic idle callback (used to free retired filter
// chains outside the real-time thread).

#include <EqConfig.h>

#include <windows.h>

#include <functional>
#include <string>
#include <thread>

namespace openeq::apo {

class ConfigWatcher {
public:
    using ConfigCallback = std::function<void(const EqConfig&)>;
    using IdleCallback = std::function<void()>;

    ConfigWatcher() = default;
    ~ConfigWatcher();
    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    // Reads the file once synchronously (calling onConfig if it parses), then
    // starts the watcher thread.
    void start(std::wstring directory, std::wstring fileName, ConfigCallback onConfig, IdleCallback onIdle);
    void stop();

private:
    void run();
    bool reload(); // returns true if the config changed

    std::wstring m_dir;
    std::wstring m_path;
    ConfigCallback m_onConfig;
    IdleCallback m_onIdle;
    std::string m_lastText;
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;
};

} // namespace openeq::apo
