// openeq-devicectl: command-line installer for the openeq APO.
//
//   openeq-devicectl list
//   openeq-devicectl status <endpoint-guid>
//   openeq-devicectl install <endpoint-guid> [--no-restart]
//   openeq-devicectl uninstall <endpoint-guid> [--no-restart]
//   openeq-devicectl uninstall-all [--no-restart]
//   openeq-devicectl restart-audio
//
// Exit codes: 0 ok, 1 failed (an install that breaks playback is rolled back),
// 2 usage, 3 not elevated, 4 installed and playback works but Windows did not
// start openeq on the device.
// Every run also writes %ProgramData%\openeq\devicectl-last.log so the UI can
// show what happened (it cannot read our console when it launches us elevated).

#include "Installer.h"

#include <Endpoints.h>
#include <OpenEqShared.h>

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace openeq;

namespace {

std::wstring g_logText;

void out(const std::wstring& s)
{
    std::string utf8(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
    if (!utf8.empty()) utf8.pop_back();
    std::fputs(utf8.c_str(), stdout);
    std::fputs("\n", stdout);
    g_logText += s + L"\r\n";
}

void flushLog()
{
    const std::wstring dir = ctl::dataDirectory();
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\devicectl-last.log";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const int n = WideCharToMultiByte(CP_UTF8, 0, g_logText.c_str(), static_cast<int>(g_logText.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string utf8(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, g_logText.c_str(), static_cast<int>(g_logText.size()), utf8.data(), n, nullptr,
                        nullptr);
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}

int usage()
{
    out(L"usage: openeq-devicectl list | status <guid> | install <guid> | uninstall <guid> | uninstall-all | "
        L"restart-audio   [--no-restart]");
    return 2;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::vector<std::wstring> args;
    bool restart = true;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--no-restart") restart = false;
        else args.push_back(a);
    }
    if (args.empty()) return usage();
    const std::wstring& cmd = args[0];

    if (cmd == L"list") {
        for (const auto& e : shared::enumerateRenderEndpoints())
            out(e.guid + L"\t" + (shared::isInstalledOn(e.guid) ? L"installed" : L"not-installed") + L"\t" +
                (e.isDefault ? L"default" : L"-") + L"\t" + e.name);
        return 0;
    }
    if (cmd == L"status") {
        if (args.size() < 2) return usage();
        const bool installed = shared::isInstalledOn(args[1]);
        out(installed ? L"installed" : L"not-installed");
        return installed ? 0 : 1;
    }

    if (!ctl::isElevated()) {
        out(L"This command needs administrator rights.");
        return 3;
    }

    ctl::Installer installer(out);
    bool ok = false;
    int code = 0;

    if (cmd == L"install" && args.size() >= 2) {
        const auto logOffset = ctl::Installer::apoLogSize();
        ok = installer.install(args[1]);
        if (ok && restart) {
            installer.restartAudioService();
            // Never leave the user without sound: if the device can no longer
            // play, put everything back the way it was.
            switch (installer.verifyEndpoint(args[1], logOffset)) {
            case ctl::Installer::Verify::Active:
                break;
            case ctl::Installer::Verify::PlaysButInactive:
                code = 4;
                break;
            case ctl::Installer::Verify::NoPlayback:
                out(L"Rolling back so the device works again...");
                installer.uninstall(args[1]);
                installer.restartAudioService();
                out(L"The device was restored to its original settings.");
                ok = false;
                break;
            }
        }
    } else if (cmd == L"uninstall" && args.size() >= 2) {
        ok = installer.uninstall(args[1]);
        if (ok && restart) ok = installer.restartAudioService();
    } else if (cmd == L"uninstall-all") {
        ok = installer.uninstallAll();
        if (ok && restart) ok = installer.restartAudioService();
    } else if (cmd == L"restart-audio") {
        ok = installer.restartAudioService();
    } else {
        return usage();
    }

    if (!ok) code = 1;
    out(code == 0 ? L"OK" : code == 4 ? L"INSTALLED (not active)" : L"FAILED");
    flushLog();
    CoUninitialize();
    return code;
}
