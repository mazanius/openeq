#pragma once
// Installs and removes the openeq APO on render endpoints. Requires elevation.

#include <functional>
#include <string>

namespace openeq::ctl {

using Logger = std::function<void(const std::wstring&)>;

class Installer {
public:
    explicit Installer(Logger log) : m_log(std::move(log)) {}

    bool install(const std::wstring& guid);
    bool uninstall(const std::wstring& guid);
    bool uninstallAll();

    // Copies openeq_apo.dll to %ProgramFiles%\openeq and registers it.
    bool installDll();
    bool unregisterDll();

    bool restartAudioService();

    // After an install + audio restart: can the endpoint still play, and did
    // audiodg actually start openeq on it?
    enum class Verify { Active, PlaysButInactive, NoPlayback };
    Verify verifyEndpoint(const std::wstring& guid, unsigned long long apoLogOffset);

    // Current size of apo.log; verifyEndpoint only looks at text after it.
    static unsigned long long apoLogSize();

    // %ProgramData%\openeq with an ACL that lets users write configs and
    // LOCAL SERVICE (audiodg) read them and write its log.
    bool ensureDataDirs();

private:
    void log(const std::wstring& s) { m_log(s); }
    Logger m_log;
};

bool isElevated();
std::wstring dataDirectory();       // %ProgramData%\openeq
std::wstring installDirectory();    // %ProgramFiles%\openeq

} // namespace openeq::ctl
